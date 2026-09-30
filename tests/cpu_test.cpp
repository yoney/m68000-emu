#include "m68000/bus.hpp"
#include "m68000/cpu.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <map>
#include <vector>

namespace {

class FakeBus : public m68000::Bus {
public:
    struct ReadAccess {
        std::uint32_t address;
        std::uint8_t size; // 8 or 16
    };

    struct WriteAccess {
        std::uint32_t address;
        std::uint16_t value;
        std::uint8_t size; // 8 or 16
    };

    std::vector<ReadAccess> reads;
    std::vector<WriteAccess> writes;
    std::map<std::uint32_t, std::uint8_t> memory;

    std::uint8_t read8(std::uint32_t address) override {
        reads.push_back({address, 8});
        return peek8(address);
    }

    std::uint16_t read16(std::uint32_t address) override {
        reads.push_back({address, 16});
        return peek16(address);
    }

    void write8(std::uint32_t address, std::uint8_t value) override {
        writes.push_back({address, static_cast<std::uint16_t>(value), 8});
        poke8(address, value);
    }

    void write16(std::uint32_t address, std::uint16_t value) override {
        writes.push_back({address, value, 16});
        poke16(address, value);
    }

    void poke8(std::uint32_t address, std::uint8_t value) {
        store_byte(address, value);
    }

    void poke16(std::uint32_t address, std::uint16_t value) {
        store_byte(address, static_cast<std::uint8_t>(value >> 8));
        store_byte(address + 1U, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void poke32(std::uint32_t address, std::uint32_t value) {
        poke16(address, static_cast<std::uint16_t>(value >> 16));
        poke16(address + 2U, static_cast<std::uint16_t>(value & 0xFFFFU));
    }

    [[nodiscard]] std::uint8_t peek8(std::uint32_t address) const {
        return load_byte(address);
    }

    [[nodiscard]] std::uint16_t peek16(std::uint32_t address) const {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(load_byte(address)) << 8) |
            static_cast<std::uint32_t>(load_byte(address + 1U)));
    }

    [[nodiscard]] std::uint32_t peek32(std::uint32_t address) const {
        return (static_cast<std::uint32_t>(peek16(address)) << 16) |
               static_cast<std::uint32_t>(peek16(address + 2U));
    }

private:
    [[nodiscard]] std::uint8_t load_byte(std::uint32_t address) const {
        auto it = memory.find(address);
        return (it != memory.end()) ? it->second : std::uint8_t{0};
    }

    void store_byte(std::uint32_t address, std::uint8_t value) {
        memory[address] = value;
    }
};

class CpuTest : public ::testing::Test {
protected:
    static constexpr std::uint32_t kDefaultPc = 0x00001000U;
    static constexpr std::uint32_t kDefaultSp = 0x00200000U;

    FakeBus bus;
    m68000::Cpu cpu;

    void SetUp() override {
        bus.poke32(0x000000U, kDefaultSp);
        bus.poke32(0x000004U, kDefaultPc);
        cpu.reset(bus);
        bus.reads.clear();
        bus.writes.clear();
    }

    void load_program(std::initializer_list<std::uint16_t> opcodes,
                      std::uint32_t address = kDefaultPc) {
        for (std::uint16_t op : opcodes) {
            bus.poke16(address, op);
            address += 2U;
        }
    }

    [[nodiscard]] bool flag_c() const noexcept {
        return (cpu.status() & m68000::carry_flag) != 0;
    }

    [[nodiscard]] bool flag_v() const noexcept {
        return (cpu.status() & m68000::overflow_flag) != 0;
    }

    [[nodiscard]] bool flag_z() const noexcept {
        return (cpu.status() & m68000::zero_flag) != 0;
    }

    [[nodiscard]] bool flag_n() const noexcept {
        return (cpu.status() & m68000::negative_flag) != 0;
    }

    [[nodiscard]] bool flag_x() const noexcept {
        return (cpu.status() & m68000::extend_flag) != 0;
    }

    [[nodiscard]] std::uint16_t flags_nzvc() const noexcept {
        return static_cast<std::uint16_t>(cpu.status() & m68000::nzvc_flags);
    }
};

} // namespace

TEST(FakeBusTest, SixteenBitWriteIsVisibleThroughTwoEightBitReads) {
    FakeBus bus;
    bus.write16(0x1000U, 0x1234U);

    EXPECT_EQ(bus.read8(0x1000U), 0x12U);
    EXPECT_EQ(bus.read8(0x1001U), 0x34U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x1000U);
    EXPECT_EQ(bus.writes[0].value, 0x1234U);
    EXPECT_EQ(bus.writes[0].size, 16U);

    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[0].address, 0x1000U);
    EXPECT_EQ(bus.reads[0].size, 8U);
    EXPECT_EQ(bus.reads[1].address, 0x1001U);
    EXPECT_EQ(bus.reads[1].size, 8U);
}

TEST(FakeBusTest, TwoEightBitWritesAreVisibleThroughOneSixteenBitRead) {
    FakeBus bus;
    bus.write8(0x2000U, 0xABU);
    bus.write8(0x2001U, 0xCDU);

    EXPECT_EQ(bus.read16(0x2000U), 0xABCDU);

    ASSERT_EQ(bus.writes.size(), 2U);
    EXPECT_EQ(bus.writes[0].address, 0x2000U);
    EXPECT_EQ(bus.writes[0].value, 0xABU);
    EXPECT_EQ(bus.writes[0].size, 8U);
    EXPECT_EQ(bus.writes[1].address, 0x2001U);
    EXPECT_EQ(bus.writes[1].value, 0xCDU);
    EXPECT_EQ(bus.writes[1].size, 8U);

    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, 0x2000U);
    EXPECT_EQ(bus.reads[0].size, 16U);
}

TEST(FakeBusTest, PokeAndPeekUseBigEndianAndBypassLogging) {
    FakeBus bus;
    bus.poke16(0x3000U, 0xCAFEU);
    bus.poke8(0x3002U, 0xBAU);

    EXPECT_EQ(bus.peek16(0x3000U), 0xCAFEU);
    EXPECT_EQ(bus.peek8(0x3000U), 0xCAU);
    EXPECT_EQ(bus.peek8(0x3001U), 0xFEU);
    EXPECT_EQ(bus.peek8(0x3002U), 0xBAU);

    EXPECT_TRUE(bus.reads.empty());
    EXPECT_TRUE(bus.writes.empty());
}

TEST(FakeBusTest, UninitializedAddressReturnsZeroWithoutModifyingMemoryMap) {
    FakeBus bus;
    EXPECT_EQ(bus.peek8(0x4000U), 0U);
    EXPECT_EQ(bus.peek16(0x4002U), 0U);
    EXPECT_EQ(bus.read8(0x4004U), 0U);
    EXPECT_EQ(bus.read16(0x4006U), 0U);

    EXPECT_TRUE(bus.memory.empty());
}

TEST(CpuResetTest, CanBeConstructed) {
    [[maybe_unused]] m68000::Cpu cpu;
    SUCCEED();
}

TEST(CpuResetTest, ResetReadsVectorTableInCorrectOrder) {
    FakeBus bus;
    m68000::Cpu cpu;

    cpu.reset(bus);

    // MC68000 reset sequence reads 4 consecutive 16-bit words:
    // 1. Initial SSP: 0x000000 (high word) and 0x000002 (low word)
    // 2. Initial PC:  0x000004 (high word) and 0x000006 (low word)
    ASSERT_EQ(bus.reads.size(), 4U);

    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_EQ(bus.reads[0].address, 0x000000U);

    EXPECT_EQ(bus.reads[1].size, 16U);
    EXPECT_EQ(bus.reads[1].address, 0x000002U);

    EXPECT_EQ(bus.reads[2].size, 16U);
    EXPECT_EQ(bus.reads[2].address, 0x000004U);

    EXPECT_EQ(bus.reads[3].size, 16U);
    EXPECT_EQ(bus.reads[3].address, 0x000006U);
}

TEST(CpuResetTest, ResetDoesNotPerformAnyBusWrites) {
    FakeBus bus;
    m68000::Cpu cpu;

    cpu.reset(bus);

    EXPECT_TRUE(bus.writes.empty());
}

TEST(CpuResetTest, ResetSetsRegistersToLoadedValues) {
    FakeBus bus;
    bus.poke16(0x000000U, 0x0020U);
    bus.poke16(0x000002U, 0x4000U);
    bus.poke16(0x000004U, 0x0001U);
    bus.poke16(0x000006U, 0x8000U);

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_EQ(cpu.A(7), 0x00204000U);
    EXPECT_EQ(cpu.pc(), 0x00018000U);
    EXPECT_EQ(cpu.status(), 0x2700U);
}

TEST(CpuResetTest, ResetHandlesFull32BitRangeWithoutSignExtension) {
    FakeBus bus;
    bus.poke16(0x000000U, 0xFFFFU);
    bus.poke16(0x000002U, 0xFFFFU);
    bus.poke16(0x000004U, 0x8000U);
    bus.poke16(0x000006U, 0x0001U);

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_EQ(cpu.A(7), 0xFFFFFFFFU);
    EXPECT_EQ(cpu.pc(), 0x80000001U);
}

TEST_F(CpuTest, StepExecutesNopAndIncrementsPc) {
    load_program({0x4E71U});

    const auto initial_status = cpu.status();
    const auto initial_sp = cpu.A(7);

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_EQ(cpu.status(), initial_status);
    EXPECT_EQ(cpu.A(7), initial_sp);
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc);
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, StepExecutesMultipleNopsSequentially) {
    load_program({0x4E71U, 0x4E71U});

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, StepThrowsOnUnsupportedInstruction) {
    load_program({0x1234U});

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, MoveqLoadsPositiveImmediateAndClearsFlags) {
    load_program({0x762AU}); // MOVEQ #42, D3

    cpu.step(bus);

    EXPECT_EQ(cpu.D(3), 42U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_EQ(flags_nzvc(), 0U);
}

TEST_F(CpuTest, MoveqSignExtendsNegativeImmediateAndSetsNegativeFlag) {
    load_program({0x70FFU}); // MOVEQ #-1, D0

    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFFFFU);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, MoveqSetsZeroFlagWhenImmediateIsZero) {
    load_program({0x7A00U}); // MOVEQ #0, D5

    cpu.step(bus);

    EXPECT_EQ(cpu.D(5), 0U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, MoveqSignExtendsMinimumImmediate) {
    load_program({0x7080U}); // MOVEQ #-128, D0

    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF80U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, MoveqLoadsMaximumPositiveImmediate) {
    load_program({0x707FU}); // MOVEQ #127, D0

    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0x0000007FU);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, BraShortForwardBranchesCorrectlyAndPreservesFlags) {
    load_program(
        {0x6004U, 0x4E71U, 0x4E71U}); // BRA.S *+6 (offset +4 from PC+2)
    const auto initial_status = cpu.status();

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
    EXPECT_EQ(cpu.status(), initial_status);
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc);
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BraShortBackwardBranchesToSelf) {
    load_program({0x60FEU}); // BRA.S * (offset -2 from PC+2)

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc);
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BraShortSignExtendsMinimumDisplacement) {
    load_program({0x6080U}); // BRA.S -128

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U - 128U);
}

TEST_F(CpuTest, BraWordForwardBranchesCorrectlyAndPreservesFlags) {
    load_program({0x6000U, 0x0006U, 0x4E71U,
                  0x4E71U}); // BRA.W *+8 (offset +6 from PC+2)
    const auto initial_status = cpu.status();

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
    EXPECT_EQ(cpu.status(), initial_status);
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc);
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_EQ(bus.reads[1].address, kDefaultPc + 2U);
    EXPECT_EQ(bus.reads[1].size, 16U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BraWordBackwardBranchesToSelf) {
    load_program({0x6000U, 0xFFFEU}); // BRA.W * (offset -2 from PC+2)

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc);
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BraWordZeroDisplacementBranchesToDisplacementWord) {
    load_program({0x6000U, 0x0000U}); // BRA.W offset 0 from PC+2

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
}

TEST_F(CpuTest, BraWordSignExtendsNegativeDisplacement) {
    load_program({0x6000U, 0xFF00U}); // BRA.W -256

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U - 256U);
}

TEST_F(CpuTest, BraTreatsFFAsShortMinusOne) {
    load_program({0x60FFU});

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 1U);
    ASSERT_EQ(bus.reads.size(), 1U);
}

TEST_F(CpuTest, BccShortBranchesWhenConditionIsTrue) {
    load_program(
        {0x7000U, 0x6704U, 0x4E71U, 0x4E71U}); // MOVEQ #0, D0 (Z=1); BEQ.S *+6
    cpu.step(bus);                             // MOVEQ #0, D0
    bus.reads.clear();

    const auto status_before = cpu.status();
    cpu.step(bus); // BEQ.S *+6

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
    EXPECT_EQ(cpu.status(), status_before);
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BccShortDoesNotBranchWhenConditionIsFalse) {
    load_program({0x7001U, 0x6704U, 0x4E71U}); // MOVEQ #1, D0 (Z=0); BEQ.S *+6
    cpu.step(bus);                             // MOVEQ #1, D0
    bus.reads.clear();

    const auto status_before = cpu.status();
    cpu.step(bus); // BEQ.S *+6

    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
    EXPECT_EQ(cpu.status(), status_before);
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BccWordBranchesWhenConditionIsTrue) {
    load_program({0x7000U, 0x6700U, 0x0006U, 0x4E71U,
                  0x4E71U}); // MOVEQ #0, D0 (Z=1); BEQ.W *+8
    cpu.step(bus);           // MOVEQ #0, D0
    bus.reads.clear();

    const auto status_before = cpu.status();
    cpu.step(bus); // BEQ.W *+8

    EXPECT_EQ(cpu.pc(), kDefaultPc + 10U);
    EXPECT_EQ(cpu.status(), status_before);
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U);
    EXPECT_EQ(bus.reads[1].address, kDefaultPc + 4U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BccWordAdvancesPcAndReadsExtensionWhenConditionIsFalse) {
    load_program({0x7001U, 0x6700U, 0x0006U,
                  0x4E71U}); // MOVEQ #1, D0 (Z=0); BEQ.W *+8; NOP
    cpu.step(bus);           // MOVEQ #1, D0
    bus.reads.clear();

    const auto status_before = cpu.status();
    cpu.step(bus); // BEQ.W *+8 (not taken)

    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
    EXPECT_EQ(cpu.status(), status_before);
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U);
    EXPECT_EQ(bus.reads[1].address, kDefaultPc + 4U);
    EXPECT_TRUE(bus.writes.empty());

    // Next step executes the following NOP at kDefaultPc + 6U
    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, BneBranchesWhenZeroFlagIsClear) {
    load_program({0x7001U, 0x6604U}); // MOVEQ #1, D0 (Z=0); BNE.S *+6
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, BneDoesNotBranchWhenZeroFlagIsSet) {
    load_program({0x7000U, 0x6604U}); // MOVEQ #0, D0 (Z=1); BNE.S *+6
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, BmiBranchesWhenNegativeFlagIsSet) {
    load_program({0x70FFU, 0x6B04U}); // MOVEQ #-1, D0 (N=1); BMI.S *+6
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, BplBranchesWhenNegativeFlagIsClear) {
    load_program({0x7001U, 0x6A04U}); // MOVEQ #1, D0 (N=0); BPL.S *+6
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, BsrShortPushesReturnAddressAndBranches) {
    load_program({0x6104U, 0x4E71U, 0x4E71U}); // BSR.S *+6
    const auto initial_sp = cpu.A(7);
    const auto initial_status = cpu.status();

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(cpu.status(), initial_status);

    // Verify 32-bit return address (kDefaultPc + 2) was pushed onto the stack
    EXPECT_EQ(bus.peek16(initial_sp - 4U),
              static_cast<std::uint16_t>((kDefaultPc + 2U) >> 16));
    EXPECT_EQ(bus.peek16(initial_sp - 2U),
              static_cast<std::uint16_t>((kDefaultPc + 2U) & 0xFFFFU));

    ASSERT_EQ(bus.reads.size(), 1U);
    ASSERT_EQ(bus.writes.size(), 2U);
    EXPECT_EQ(bus.writes[0].address, initial_sp - 4U);
    EXPECT_EQ(bus.writes[1].address, initial_sp - 2U);
}

TEST_F(CpuTest, BsrWordPushesReturnAddressAndBranches) {
    load_program({0x6100U, 0x0006U, 0x4E71U, 0x4E71U}); // BSR.W *+8
    const auto initial_sp = cpu.A(7);
    const auto initial_status = cpu.status();

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(cpu.status(), initial_status);

    // Verify 32-bit return address (kDefaultPc + 4) was pushed onto the stack
    EXPECT_EQ(bus.peek16(initial_sp - 4U),
              static_cast<std::uint16_t>((kDefaultPc + 4U) >> 16));
    EXPECT_EQ(bus.peek16(initial_sp - 2U),
              static_cast<std::uint16_t>((kDefaultPc + 4U) & 0xFFFFU));

    ASSERT_EQ(bus.reads.size(), 2U);
    ASSERT_EQ(bus.writes.size(), 2U);
    EXPECT_EQ(bus.writes[0].address, initial_sp - 4U);
    EXPECT_EQ(bus.writes[1].address, initial_sp - 2U);
}

TEST_F(CpuTest, RtsPopsReturnAddressAndIncrementsStackPointer) {
    const std::uint32_t target_pc = 0x00004000U;
    const auto initial_sp = cpu.A(7);
    const auto initial_status = cpu.status();

    // Place 32-bit return address onto the stack (big-endian)
    bus.poke32(initial_sp, target_pc);

    load_program({0x4E75U}); // RTS

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), target_pc);
    EXPECT_EQ(cpu.A(7), initial_sp + 4U);
    EXPECT_EQ(cpu.status(), initial_status);

    ASSERT_EQ(bus.reads.size(), 3U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc);
    EXPECT_EQ(bus.reads[1].address, initial_sp);
    EXPECT_EQ(bus.reads[2].address, initial_sp + 2U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, BsrAndRtsExecuteSubroutineRoundTrip) {
    const auto initial_sp = cpu.A(7);

    // Main routine:
    //   0x1000: BSR.S +4 (jump to subroutine at 0x1006)
    //   0x1002: MOVEQ #42, D0 (return target)
    //   0x1004: NOP
    // Subroutine:
    //   0x1006: RTS
    load_program({
        0x6104U, // BSR.S *+6
        0x702AU, // MOVEQ #42, D0
        0x4E71U, // NOP
        0x4E75U  // RTS
    });

    // 1. Execute BSR.S: calls subroutine, pushes return address (kDefaultPc +
    // 2)
    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);

    // 2. Execute RTS: returns to kDefaultPc + 2 and restores stack pointer
    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
    EXPECT_EQ(cpu.A(7), initial_sp);

    // 3. Execute MOVEQ #42, D0: confirms execution resumed at caller
    cpu.step(bus);
    EXPECT_EQ(cpu.D(0), 42U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, AddqByteToDataRegisterModifiesOnlyLowByte) {
    // Set D0 = 0x12345678 via MOVEQ then shift/load or setup
    load_program({
        0x7078U, // MOVEQ #0x78, D0
        0x5200U  // ADDQ.B #1, D0
    });

    cpu.step(bus); // MOVEQ
    EXPECT_EQ(cpu.D(0), 0x00000078U);

    cpu.step(bus); // ADDQ.B #1, D0
    EXPECT_EQ(cpu.D(0), 0x00000079U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_x());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, AddqQuickDataZeroEncodesEight) {
    load_program({
        0x7002U, // MOVEQ #2, D0
        0x5000U  // ADDQ.B #8, D0 (data field 000 = 8)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 10U);
}

TEST_F(CpuTest, AddqSetsZeroAndCarryFlagsOnWrap) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 (0xFFFFFFFF)
        0x5200U  // ADDQ.B #1, D0 -> byte becomes 0x00, upper bits preserved
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF00U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_TRUE(flag_c());
    EXPECT_TRUE(flag_x());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, AddqSetsNegativeAndOverflowFlags) {
    load_program({
        0x707FU, // MOVEQ #127, D0 (0x7F)
        0x5200U  // ADDQ.B #1, D0 -> 0x80 (-128 signed), overflow!
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0x00000080U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_x());
    EXPECT_TRUE(flag_v());
}

TEST_F(CpuTest, SubqSubtractsAndSetsZeroFlag) {
    load_program({
        0x7005U, // MOVEQ #5, D0
        0x5B00U  // SUBQ.B #5, D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_x());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, SubqSetsCarryAndExtendOnBorrow) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5300U  // SUBQ.B #1, D0 -> 0xFF (-1), borrow occurred!
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0x000000FFU);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c());
    EXPECT_TRUE(flag_x());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, SubqSetsOverflowWhenNegativeBecomesPositive) {
    load_program({
        0x7080U, // MOVEQ #-128, D0 (0xFFFFFF80)
        0x5300U  // SUBQ.B #1, D0 -> byte wraps to 0x7F (+127), overflow!
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF7FU);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_x());
    EXPECT_TRUE(flag_v());
}

TEST_F(CpuTest, AddqWordToAddressRegisterAffectsFull32BitAndPreservesFlags) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 (sets N flag)
        0x5248U  // ADDQ.W #1, A0 (A0 is initially 0)
    });

    cpu.step(bus);
    const auto status_before = cpu.status();

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 1U);
    EXPECT_EQ(cpu.status(), status_before); // Flags unchanged!
}

TEST_F(CpuTest, AddqWordToAddressRegisterPropagatesCarryIntoUpperBits) {
    // Initialize A0 to 0x0001FFFF via stack pointer or by resetting A0
    // We can simulate an initial A0 by writing to reset vector or executing
    // instructions. Here, let's execute ADDQ.L to build up A0.
    load_program({
        0x5248U // ADDQ.W #1, A0
    });

    // Reset A0 to 0x0001FFFF by writing to bus reset vector if needed,
    // or we can test full 32-bit addition.
    // A0 is 0 by default. Let's do 32-bit SUBQ to make it 0xFFFFFFFF
    load_program({
        0x5388U, // SUBQ.L #1, A0 -> 0xFFFFFFFF
        0x5248U  // ADDQ.W #1, A0 -> wraps to 0x00000000 (full 32-bit update)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 0xFFFFFFFFU);

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 0x00000000U); // Proves 32-bit wrap, not 16-bit!
}

TEST_F(CpuTest, AddqByteToAddressRegisterThrowsUnsupportedInstruction) {
    load_program({
        0x5208U // ADDQ.B #1, A0 (illegal size on An)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, AddqUnsupportedModeThrows) {
    load_program({
        0x527AU // ADDQ.W #1, d16(PC) (mode 7, reg 2, non-alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, AddqProgramCounterIndexDestinationThrows) {
    load_program({
        0x527BU // ADDQ.W #1, d8(PC, D0) (mode 7, reg 3, non-alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, AddqByteToMemoryIndirect) {
    bus.poke8(kDefaultSp, 0x41U);
    load_program({
        0x5217U // ADDQ.B #1, (A7)
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(kDefaultSp), 0x42U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, AddqWordToMemoryIndirect) {
    bus.poke16(kDefaultSp, 0x1234U);
    load_program({
        0x5257U // ADDQ.W #1, (A7)
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek16(kDefaultSp), 0x1235U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqLongToMemoryIndirect) {
    bus.poke16(kDefaultSp, 0x0001U);
    bus.poke16(kDefaultSp + 2U, 0xFFFFU);
    load_program({
        0x5297U // ADDQ.L #1, (A7)
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek16(kDefaultSp), 0x0002U);
    EXPECT_EQ(bus.peek16(kDefaultSp + 2U), 0x0000U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SubqByteToMemoryIndirectSetsZeroFlag) {
    bus.poke8(kDefaultSp, 0x05U);
    load_program({
        0x5B17U // SUBQ.B #5, (A7)
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(kDefaultSp), 0x00U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqWordToAddressRegisterIndirectA0) {
    bus.poke16(0x00000008U, 0x1000U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5250U  // ADDQ.W #1, (A0)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 8U);

    cpu.step(bus);
    EXPECT_EQ(bus.peek16(0x00000008U), 0x1001U);
}

TEST_F(CpuTest, AddqByteToPostIncrementGeneralRegisterIncrementsByOne) {
    bus.poke8(0x00000008U, 0x41U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5218U  // ADDQ.B #1, (A0)+
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 8U);

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000008U), 0x42U);
    EXPECT_EQ(cpu.A(0), 9U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());

    // Both read and write must access original address 0x08, not
    // post-incremented 0x09
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[1].address, 0x00000008U);
    EXPECT_EQ(bus.reads[1].size, 8U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x00000008U);
    EXPECT_EQ(bus.writes[0].value, 0x42U);
    EXPECT_EQ(bus.writes[0].size, 8U);
}

TEST_F(CpuTest, AddqByteToPostIncrementStackPointerIncrementsByTwo) {
    bus.poke8(kDefaultSp, 0x41U);
    load_program({
        0x521FU // ADDQ.B #1, (A7)+
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(kDefaultSp), 0x42U);
    EXPECT_EQ(cpu.A(7), kDefaultSp + 2U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqWordToPostIncrementIncrementsByTwo) {
    bus.poke16(0x00000008U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5258U  // ADDQ.W #1, (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000008U), 0x1235U);
    EXPECT_EQ(cpu.A(0), 10U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqLongToPostIncrementIncrementsByFour) {
    bus.poke16(0x00000008U, 0x0001U);
    bus.poke16(0x0000000AU, 0xFFFFU);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5298U  // ADDQ.L #1, (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000008U), 0x0002U);
    EXPECT_EQ(bus.peek16(0x0000000AU), 0x0000U);
    EXPECT_EQ(cpu.A(0), 12U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SubqByteToPostIncrementSetsFlagsAndStillIncrements) {
    bus.poke8(0x00000008U, 0x05U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5B18U  // SUBQ.B #5, (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek8(0x00000008U), 0x00U);
    EXPECT_EQ(cpu.A(0), 9U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqPostIncrementSequentiallyAdvancesPointer) {
    bus.poke8(0x00000008U, 0x10U);
    bus.poke8(0x00000009U, 0x20U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x5218U, // ADDQ.B #1, (A0)+
        0x5218U  // ADDQ.B #1, (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000008U), 0x11U);
    EXPECT_EQ(cpu.A(0), 9U);

    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000009U), 0x21U);
    EXPECT_EQ(cpu.A(0), 10U);
}

TEST_F(CpuTest, AddqByteToPreDecrementGeneralRegisterDecrementsByOne) {
    bus.poke8(0x00000007U, 0x41U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5220U  // ADDQ.B #1, -(A0)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 8U);

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000007U), 0x42U);
    EXPECT_EQ(cpu.A(0), 7U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());

    // Both read and write must access the decremented address 0x07
    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[1].address, 0x00000007U);
    EXPECT_EQ(bus.reads[1].size, 8U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x00000007U);
    EXPECT_EQ(bus.writes[0].value, 0x42U);
    EXPECT_EQ(bus.writes[0].size, 8U);
}

TEST_F(CpuTest, AddqByteToPreDecrementStackPointerDecrementsByTwo) {
    bus.poke8(kDefaultSp - 2U, 0x41U);
    load_program({
        0x5227U // ADDQ.B #1, -(A7)
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(kDefaultSp - 2U), 0x42U);
    EXPECT_EQ(cpu.A(7), kDefaultSp - 2U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqWordToPreDecrementDecrementsByTwo) {
    bus.poke16(0x00000006U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5260U  // ADDQ.W #1, -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000006U), 0x1235U);
    EXPECT_EQ(cpu.A(0), 6U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqLongToPreDecrementDecrementsByFour) {
    bus.poke16(0x00000004U, 0x0001U);
    bus.poke16(0x00000006U, 0xFFFFU);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x52A0U  // ADDQ.L #1, -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000004U), 0x0002U);
    EXPECT_EQ(bus.peek16(0x00000006U), 0x0000U);
    EXPECT_EQ(cpu.A(0), 4U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SubqByteToPreDecrementSetsFlagsAndStillDecrements) {
    bus.poke8(0x00000007U, 0x05U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5B20U  // SUBQ.B #5, -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek8(0x00000007U), 0x00U);
    EXPECT_EQ(cpu.A(0), 7U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqPreDecrementSequentiallyStepsBackward) {
    bus.poke8(0x00000007U, 0x10U);
    bus.poke8(0x00000006U, 0x20U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x5220U, // ADDQ.B #1, -(A0)
        0x5220U  // ADDQ.B #1, -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000007U), 0x11U);
    EXPECT_EQ(cpu.A(0), 7U);

    cpu.step(bus);
    EXPECT_EQ(bus.peek8(0x00000006U), 0x21U);
    EXPECT_EQ(cpu.A(0), 6U);
}

TEST_F(CpuTest, AddqByteToAddressRegisterIndirectWithPositiveDisplacement) {
    bus.poke8(0x0000000CU, 0x10U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5228U, // ADDQ.B #1, 4(A0)
        0x0004U  // displacement +4
    });

    cpu.step(bus); // A0 = 8
    EXPECT_EQ(cpu.A(0), 8U);

    bus.reads.clear();
    bus.writes.clear();

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // ADDQ.B #1, 4(A0)

    EXPECT_EQ(bus.peek8(0x0000000CU), 0x11U); // 8 + 4 = 12 (0x0C)
    EXPECT_EQ(cpu.A(0), 8U);                  // A0 must not be modified
    EXPECT_EQ(cpu.pc(), initial_pc + 4U); // PC advances past opcode + extension
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());

    ASSERT_EQ(bus.reads.size(), 3U);
    EXPECT_EQ(bus.reads[0].address, initial_pc); // Opcode fetch
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_EQ(bus.reads[1].address, initial_pc + 2U); // Extension fetch
    EXPECT_EQ(bus.reads[1].size, 16U);
    EXPECT_EQ(bus.reads[2].address, 0x0000000CU); // Operand read
    EXPECT_EQ(bus.reads[2].size, 8U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x0000000CU); // Operand write
    EXPECT_EQ(bus.writes[0].value, 0x11U);
    EXPECT_EQ(bus.writes[0].size, 8U);
}

TEST_F(CpuTest, AddqWordToAddressRegisterIndirectWithNegativeDisplacement) {
    bus.poke16(0x00000006U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5268U, // ADDQ.W #1, -2(A0)
        0xFFFEU  // displacement -2
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // ADDQ.W #1, -2(A0) -> address 6

    EXPECT_EQ(bus.peek16(0x00000006U), 0x1235U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, AddqLongToAddressRegisterIndirectWithDisplacement) {
    bus.poke16(0x0000000AU, 0x0001U);
    bus.poke16(0x0000000CU, 0xFFFFU);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x52A8U, // ADDQ.L #1, 2(A0)
        0x0002U  // displacement +2 -> address 10 (0x0A)
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // ADDQ.L #1, 2(A0)

    EXPECT_EQ(bus.peek16(0x0000000AU), 0x0002U);
    EXPECT_EQ(bus.peek16(0x0000000CU), 0x0000U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest,
       SubqByteToAddressRegisterIndirectWithNegativeDisplacementSetsFlags) {
    bus.poke8(0x00000004U, 0x05U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5B28U, // SUBQ.B #5, -4(A0)
        0xFFFCU  // displacement -4 -> address 4
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // SUBQ.B #5, -4(A0)

    EXPECT_EQ(bus.peek8(0x00000004U), 0x00U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqWordToAddressRegisterIndirectWithZeroDisplacement) {
    bus.poke16(0x00000008U, 0x2000U);
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5268U, // ADDQ.W #1, 0(A0)
        0x0000U  // displacement 0
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // ADDQ.W #1, 0(A0)

    EXPECT_EQ(bus.peek16(0x00000008U), 0x2001U);
    EXPECT_EQ(cpu.A(0), 8U);
}

TEST_F(CpuTest,
       AddqByteToAddressRegisterIndirectWithIndexWordAndPositiveDisplacement) {
    bus.poke8(0x00000016U, 0x41U); // 8 + 10 + 4 = 22 (0x16)
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x720AU, // MOVEQ #10, D1 (D1 becomes 10)
        0x5230U, // ADDQ.B #1, 4(A0, D1.W)
        0x1004U  // extension: D1.W, displacement +4
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // D1 = 10

    bus.reads.clear();
    bus.writes.clear();

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // ADDQ.B #1, 4(A0, D1.W)

    EXPECT_EQ(bus.peek8(0x00000016U), 0x42U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.D(1), 10U);
    EXPECT_EQ(cpu.pc(), initial_pc + 4U);

    ASSERT_EQ(bus.reads.size(), 3U);
    EXPECT_EQ(bus.reads[0].address, initial_pc);
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_EQ(bus.reads[1].address, initial_pc + 2U);
    EXPECT_EQ(bus.reads[1].size, 16U);
    EXPECT_EQ(bus.reads[2].address, 0x00000016U);
    EXPECT_EQ(bus.reads[2].size, 8U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x00000016U);
    EXPECT_EQ(bus.writes[0].value, 0x42U);
    EXPECT_EQ(bus.writes[0].size, 8U);
}

TEST_F(CpuTest,
       AddqWordToAddressRegisterIndirectWithIndexLongAndNegativeDisplacement) {
    bus.poke16(0x00000024U, 0x1234U); // 8 + 32 - 4 = 36 (0x24)
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x7420U, // MOVEQ #32, D2 (D2 becomes 32)
        0x5270U, // ADDQ.W #1, -4(A0, D2.L)
        0x28FCU  // extension: D2.L, displacement -4
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // D2 = 32
    cpu.step(bus); // ADDQ.W #1, -4(A0, D2.L)

    EXPECT_EQ(bus.peek16(0x00000024U), 0x1235U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.D(2), 32U);
}

TEST_F(
    CpuTest,
    AddqWordToAddressRegisterIndirectWithAddressRegisterIndexWordNegativeSignExtension) {
    bus.poke16(0x0000000AU, 0x1000U); // 8 + (-2) + 4 = 10 (0x0A)
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x5549U, // SUBQ.W #2, A1 (A1 becomes 0xFFFFFFFE, low word -2)
        0x5270U, // ADDQ.W #1, 4(A0, A1.W)
        0x9004U  // extension: A1.W, displacement +4
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // A1 = 0xFFFFFFFE
    cpu.step(bus); // ADDQ.W #1, 4(A0, A1.W)

    EXPECT_EQ(bus.peek16(0x0000000AU), 0x1001U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.A(1), 0xFFFFFFFEU);
}

TEST_F(CpuTest, SubqByteToAddressRegisterIndirectWithIndexSetsZeroFlag) {
    bus.poke8(0x00000008U, 0x05U); // 8 + 0 + 0 = 8
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 becomes 8)
        0x7000U, // MOVEQ #0, D0 (D0 becomes 0)
        0x5B30U, // SUBQ.B #5, 0(A0, D0.W)
        0x0000U  // extension: D0.W, displacement 0
    });

    cpu.step(bus); // A0 = 8
    cpu.step(bus); // D0 = 0
    cpu.step(bus); // SUBQ.B #5, 0(A0, D0.W)

    EXPECT_EQ(bus.peek8(0x00000008U), 0x00U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqByteToAbsoluteShortAddress) {
    bus.poke8(0x2000U, 0x41U);
    load_program({
        0x5238U, // ADDQ.B #1, ($2000).W
        0x2000U  // absolute short address
    });

    const auto initial_pc = cpu.pc();
    cpu.step(bus);

    EXPECT_EQ(bus.peek8(0x2000U), 0x42U);
    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());

    ASSERT_EQ(bus.reads.size(), 3U);
    EXPECT_EQ(bus.reads[0].address, initial_pc);
    EXPECT_EQ(bus.reads[0].size, 16U);
    EXPECT_EQ(bus.reads[1].address, initial_pc + 2U);
    EXPECT_EQ(bus.reads[1].size, 16U);
    EXPECT_EQ(bus.reads[2].address, 0x2000U);
    EXPECT_EQ(bus.reads[2].size, 8U);

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, 0x2000U);
    EXPECT_EQ(bus.writes[0].value, 0x42U);
    EXPECT_EQ(bus.writes[0].size, 8U);
}

TEST_F(CpuTest, AddqWordToAbsoluteShortNegativeSignExtension) {
    bus.poke16(0xFFFF8000U, 0x1234U);
    load_program({0x5278U, // ADDQ.W #1, ($8000).W (sign-extends to 0xFFFF8000)
                  0x8000U});

    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0xFFFF8000U), 0x1235U);
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, AddqLongToAbsoluteLongAddress) {
    bus.poke16(0x00020000U, 0x0001U);
    bus.poke16(0x00020002U, 0xFFFFU);
    load_program({
        0x52B9U, // ADDQ.L #1, ($00020000).L
        0x0002U, // high word of address
        0x0000U  // low word of address
    });

    const auto initial_pc = cpu.pc();
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00020000U), 0x0002U);
    EXPECT_EQ(bus.peek16(0x00020002U), 0x0000U);
    EXPECT_EQ(cpu.pc(), initial_pc + 6U); // 2 for opcode + 4 for 32-bit address
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, SubqByteToAbsoluteShortSetsZeroFlag) {
    bus.poke8(0x2000U, 0x05U);
    load_program({0x5B38U, // SUBQ.B #5, ($2000).W
                  0x2000U});

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(0x2000U), 0x00U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, AddqImmediateDestinationThrowsUnsupportedInstruction) {
    load_program({
        0x527CU // ADDQ.W #1, #5 (mode 7, reg 4, immediate is not alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, SccAndDbccOpcodeSpaceDoesNotEnterAddq) {
    load_program({
        0x51C8U // DBRA D0, label (size bits 11, should throw
                // UnsupportedInstruction)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, SubqWordPreservesUpperWord) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 -> 0xFFFFFFFF
        0x5340U  // SUBQ.W #1, D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFFFEU);
}

TEST_F(CpuTest, AddqLongSetsCarryOnWrap) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0
        0x5280U  // ADDQ.L #1, D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_z());
    EXPECT_TRUE(flag_c());
    EXPECT_TRUE(flag_x());
}

TEST_F(CpuTest, SubqLongSetsBorrowAndExtendOnBorrow) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U  // SUBQ.L #1, D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFFFFU);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c());
    EXPECT_TRUE(flag_x());
}

TEST_F(CpuTest, ClrByteDataRegisterClearsLowByteAndPreservesUpperBits) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 -> 0xFFFFFFFF
        0x4200U  // CLR.B D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF00U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, ClrWordDataRegisterClearsLowWordAndPreservesUpperWord) {
    load_program({
        0x72FFU, // MOVEQ #-1, D1 -> 0xFFFFFFFF
        0x4241U  // CLR.W D1
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(1), 0xFFFF0000U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, ClrLongDataRegisterClearsEntireRegister) {
    load_program({
        0x74FFU, // MOVEQ #-1, D2 -> 0xFFFFFFFF
        0x4282U  // CLR.L D2
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(2), 0x00000000U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, ClrPreservesExtendFlag) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags)
        0x4280U  // CLR.L D0
    });

    cpu.step(bus);
    cpu.step(bus);
    EXPECT_TRUE(flag_x());

    cpu.step(bus);
    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_x()); // X flag must be preserved
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, ClrAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4248U // CLR.W A0 (mode 1 is not alterable data)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, ClrByteMemoryIndirect) {
    constexpr std::uint32_t target_address = 0x00000008U;
    bus.poke8(target_address, 0xFFU);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4210U  // CLR.B (A0)
    });

    cpu.step(bus); // ADDQ.L #8, A0

    bus.reads.clear();
    bus.writes.clear();

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CLR.B (A0)

    EXPECT_EQ(bus.peek8(target_address), 0x00U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_z());

    ASSERT_EQ(bus.reads.size(), 2U);
    EXPECT_EQ(bus.reads[0].address, initial_pc);     // opcode
    EXPECT_EQ(bus.reads[1].address, target_address); // old value

    ASSERT_EQ(bus.writes.size(), 1U);
    EXPECT_EQ(bus.writes[0].address, target_address);
    EXPECT_EQ(bus.writes[0].value, 0U);
}

TEST_F(CpuTest, ClrWordPostIncrementClearsMemoryAndAdvancesAddress) {
    bus.poke16(0x00000008U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4258U  // CLR.W (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000008U), 0x0000U);
    EXPECT_EQ(cpu.A(0), 10U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, ClrBytePostIncrementStackPointerIncrementsByTwo) {
    bus.poke8(kDefaultSp, 0xABU);
    load_program({
        0x421FU // CLR.B (A7)+
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek8(kDefaultSp), 0x00U);
    EXPECT_EQ(cpu.A(7), kDefaultSp + 2U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, ClrPreDecrementDecrementsAddressAndClearsMemory) {
    bus.poke16(0x00000006U, 0x5678U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4260U  // CLR.W -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x00000006U), 0x0000U);
    EXPECT_EQ(cpu.A(0), 6U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, ClrDisplacement) {
    bus.poke8(0x0000000CU, 0xEEU);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4228U, // CLR.B 4(A0)
        0x0004U  // displacement +4
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(bus.peek8(0x0000000CU), 0x00U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, ClrAbsoluteAddress) {
    bus.poke16(0x2000U, 0xCAFEU);
    load_program({
        0x4278U, // CLR.W ($2000).W
        0x2000U  // absolute address
    });

    cpu.step(bus);

    EXPECT_EQ(bus.peek16(0x2000U), 0x0000U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, ClrImmediateDestinationThrowsUnsupportedInstruction) {
    load_program({
        0x427CU // CLR.W #5 (immediate is not alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, ClrProgramCounterDisplacementThrowsUnsupportedInstruction) {
    load_program({
        0x427AU // CLR.W d16(PC) (mode 7, reg 2, non-alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, ClrProgramCounterIndexThrowsUnsupportedInstruction) {
    load_program({
        0x427BU // CLR.W d8(PC, D0) (mode 7, reg 3, non-alterable)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, ClrReservedSizeThrowsUnsupportedInstruction) {
    load_program({
        0x42C0U // CLR opcode pattern with reserved size 11
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, TstByteZeroSetsZeroAndClearsNegative) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x4A00U  // TST.B D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, TstBytePositiveClearsZeroAndNegative) {
    load_program({
        0x707FU, // MOVEQ #127, D0
        0x4A00U  // TST.B D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0x7FU);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, TstByteNegativeSetsNegativeAndClearsZero) {
    load_program({
        0x7080U, // MOVEQ #-128, D0 -> 0xFFFFFF80
        0x4A00U  // TST.B D0
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF80U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, TstByteIgnoresUpperBits) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 -> 0xFFFFFFFF
        0x4200U, // CLR.B D0     -> 0xFFFFFF00
        0x4A00U  // TST.B D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFF00U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstWordZeroSetsZeroAndClearsNegative) {
    load_program({
        0x7200U, // MOVEQ #0, D1
        0x4A41U  // TST.W D1
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstWordPositiveClearsZeroAndNegative) {
    load_program({
        0x7201U, // MOVEQ #1, D1
        0x4A41U  // TST.W D1
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstWordNegativeSetsNegativeAndClearsZero) {
    load_program({
        0x72FFU, // MOVEQ #-1, D1 -> 0xFFFFFFFF
        0x4A41U  // TST.W D1
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
}

TEST_F(CpuTest, TstWordIgnoresUpperWord) {
    load_program({
        0x72FFU, // MOVEQ #-1, D1 -> 0xFFFFFFFF
        0x4241U, // CLR.W D1     -> 0xFFFF0000
        0x4A41U  // TST.W D1
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(1), 0xFFFF0000U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstLongZeroSetsZeroAndClearsNegative) {
    load_program({
        0x7400U, // MOVEQ #0, D2
        0x4A82U  // TST.L D2
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstLongPositiveClearsZeroAndNegative) {
    load_program({
        0x742AU, // MOVEQ #42, D2
        0x4A82U  // TST.L D2
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstLongNegativeSetsNegativeAndClearsZero) {
    load_program({
        0x74FFU, // MOVEQ #-1, D2 -> 0xFFFFFFFF
        0x4A82U  // TST.L D2
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
}

TEST_F(CpuTest, TstPreservesExtendFlag) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags)
        0x7200U, // MOVEQ #0, D1 (preserves X)
        0x4A81U  // TST.L D1
    });

    cpu.step(bus);
    cpu.step(bus);
    EXPECT_TRUE(flag_x());

    cpu.step(bus); // MOVEQ #0, D1
    EXPECT_TRUE(flag_x());

    cpu.step(bus); // TST.L D1
    EXPECT_TRUE(flag_z());
    EXPECT_TRUE(flag_x()); // X flag must be preserved
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, TstAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4A48U // TST.W A0 (mode 1 is illegal on MC68000)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, TstMemoryIndirectDoesNotModifyMemoryOrRegisters) {
    constexpr std::uint32_t target_address = 0x00000008U;
    bus.poke8(target_address, 0x80U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4A10U  // TST.B (A0)
    });

    cpu.step(bus); // ADDQ.L #8, A0

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus); // TST.B (A0)

    EXPECT_EQ(bus.peek8(target_address), 0x80U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());

    EXPECT_TRUE(bus.writes.empty()); // TST must not write to memory
}

TEST_F(CpuTest, TstPostIncrementAdvancesAddress) {
    bus.poke16(0x00000008U, 0x0000U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4A58U  // TST.W (A0)+
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.A(0), 10U);
    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, TstPreDecrementDecrementsAddress) {
    bus.poke16(0x00000006U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4A60U  // TST.W -(A0)
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.A(0), 6U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
}

TEST_F(CpuTest, TstDisplacement) {
    bus.poke8(0x0000000CU, 0xFFU);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4A28U, // TST.B 4(A0)
        0x0004U  // displacement +4
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_n());
}

TEST_F(CpuTest, TstAbsoluteAddress) {
    bus.poke16(0x2000U, 0x0000U);
    load_program({
        0x4A78U, // TST.W ($2000).W
        0x2000U  // absolute address
    });

    cpu.step(bus);

    EXPECT_TRUE(flag_z());
}

TEST_F(CpuTest, TstImmediateDestinationThrowsUnsupportedInstruction) {
    load_program({
        0x4A7CU // TST.W #5 (immediate is not alterable on MC68000)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, TstProgramCounterDisplacementThrowsUnsupportedInstruction) {
    load_program({
        0x4A7AU // TST.W d16(PC) (mode 7, reg 2, non-alterable on MC68000)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, TstProgramCounterIndexThrowsUnsupportedInstruction) {
    load_program({
        0x4A7BU // TST.W d8(PC, D0) (mode 7, reg 3, non-alterable on MC68000)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, TstReservedSizeThrowsUnsupportedInstruction) {
    load_program({
        0x4AC0U // TST opcode pattern with reserved size 11 (TAS space)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, CmpByteEqualSetsZeroAndClearsOthers) {
    load_program({
        0x702AU, // MOVEQ #42, D0
        0x722AU, // MOVEQ #42, D1
        0xB001U  // CMP.B D1, D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 42U); // Destination must be preserved
    EXPECT_EQ(cpu.D(1), 42U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, CmpByteGreaterClearsBorrowAndNegative) {
    load_program({
        0x7005U, // MOVEQ #5, D0
        0x7202U, // MOVEQ #2, D1
        0xB001U  // CMP.B D1, D0 (5 - 2 = 3)
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 5U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c()); // No borrow
}

TEST_F(CpuTest, CmpByteLessSetsBorrowAndNegative) {
    load_program({
        0x7002U, // MOVEQ #2, D0
        0x7205U, // MOVEQ #5, D1
        0xB001U  // CMP.B D1, D0 (2 - 5 = -3)
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 2U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_TRUE(flag_c()); // Borrow occurred
}

TEST_F(CpuTest, CmpByteOverflow) {
    load_program({
        0x707FU, // MOVEQ #127, D0
        0x7280U, // MOVEQ #-128, D1 (0xFFFFFF80)
        0xB001U  // CMP.B D1, D0 (127 - (-128) = 255 -> overflow)
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_v());
    EXPECT_TRUE(flag_n()); // Result 0xFF has sign bit set
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, CmpWordDataRegisters) {
    load_program({
        0x7001U, // MOVEQ #1, D0
        0x7202U, // MOVEQ #2, D1
        0xB041U  // CMP.W D1, D0 (1 - 2 = -1)
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c());
}

TEST_F(CpuTest, CmpLongDataRegisters) {
    load_program({
        0x700AU, // MOVEQ #10, D0
        0x720AU, // MOVEQ #10, D1
        0xB081U  // CMP.L D1, D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, CmpWordAddressRegisterSource) {
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 = 8)
        0x7008U, // MOVEQ #8, D0  (D0 = 8)
        0xB048U  // CMP.W A0, D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 8U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, CmpLongAddressRegisterSource) {
    load_program({
        0x5088U, // ADDQ.L #8, A0 (A0 = 8)
        0x7005U, // MOVEQ #5, D0  (D0 = 5)
        0xB088U  // CMP.L A0, D0 (5 - 8 = -3)
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c());
}

TEST_F(CpuTest, CmpByteAddressRegisterSourceThrowsUnsupportedInstruction) {
    load_program({
        0xB008U // CMP.B A0, D0 (byte operations on An are illegal)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, CmpPreservesExtendFlag) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags)
        0x7205U, // MOVEQ #5, D1 (preserves X)
        0xB281U  // CMP.L D1, D1 (equal, Z=1, N=0, C=0, X unaffected)
    });

    cpu.step(bus);
    cpu.step(bus);
    EXPECT_TRUE(flag_x());

    cpu.step(bus); // MOVEQ #5, D1
    EXPECT_TRUE(flag_x());

    cpu.step(bus); // CMP.L D1, D1
    EXPECT_TRUE(flag_z());
    EXPECT_TRUE(flag_x()); // X flag must be preserved
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, CmpMemoryIndirectReadsWithoutWriting) {
    constexpr std::uint32_t target_address = 0x00000008U;
    bus.poke8(target_address, 0x05U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7005U, // MOVEQ #5, D0
        0xB010U  // CMP.B (A0), D0
    });

    cpu.step(bus);
    cpu.step(bus);

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus); // CMP.B (A0), D0

    EXPECT_TRUE(flag_z());
    EXPECT_EQ(cpu.D(0), 5U);
    EXPECT_EQ(bus.peek8(target_address), 0x05U);
    EXPECT_TRUE(bus.writes.empty()); // CMP must not write to memory
}

TEST_F(CpuTest, CmpPostIncrementAdvancesAddress) {
    bus.poke16(0x00000008U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7000U, // MOVEQ #0, D0
        0xB058U  // CMP.W (A0)+, D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.A(0), 10U);
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, CmpPreDecrementDecrementsAddress) {
    bus.poke16(0x00000006U, 0x1234U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7000U, // MOVEQ #0, D0
        0xB060U  // CMP.W -(A0), D0
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.A(0), 6U);
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, CmpDisplacement) {
    bus.poke8(0x0000000CU, 0x20U);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7020U, // MOVEQ #32, D0
        0xB028U, // CMP.B 4(A0), D0
        0x0004U  // displacement +4
    });

    cpu.step(bus);
    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_z());
    EXPECT_EQ(cpu.A(0), 8U);
}

TEST_F(CpuTest, CmpAbsoluteAddress) {
    bus.poke16(0x2000U, 0xCAFEU);
    load_program({
        0x7000U, // MOVEQ #0, D0
        0xB078U, // CMP.W ($2000).W, D0
        0x2000U  // absolute address
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_c()); // 0 < 0xCAFE -> borrow
}

TEST_F(CpuTest, CmpByteImmediateEqual) {
    load_program({
        0x702AU, // MOVEQ #42, D0
        0xB03CU, // CMP.B #42, D0
        0x002AU  // immediate byte 42 (in low-order byte)
    });

    cpu.step(bus); // MOVEQ

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.B

    EXPECT_EQ(cpu.D(0), 42U);
    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpByteImmediateLessSetsBorrow) {
    load_program({
        0x700AU, // MOVEQ #10, D0
        0xB03CU, // CMP.B #20, D0
        0x0014U  // immediate byte 20
    });

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 10U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c()); // 10 < 20 -> borrow
}

TEST_F(CpuTest, CmpWordImmediateSetsBorrow) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0xB07CU, // CMP.W #$1234, D0
        0x1234U  // immediate word
    });

    cpu.step(bus);

    const auto initial_pc = cpu.pc();
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_c()); // 0 < 0x1234 -> borrow
}

TEST_F(CpuTest, CmpLongImmediateSetsBorrow) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0xB0BCU, // CMP.L #$12345678, D0
        0x1234U, // immediate long high word
        0x5678U  // immediate long low word
    });

    cpu.step(bus);

    const auto initial_pc = cpu.pc();
    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), initial_pc + 6U); // opcode (2) + long imm (4)
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_c()); // 0 < 0x12345678 -> borrow
}

TEST_F(CpuTest, CmpLongImmediateExactMatch) {
    load_program({0x70FFU, // MOVEQ #-1, D0 -> 0xFFFFFFFF
                  0xB0BCU, // CMP.L #$FFFFFFFF, D0
                  0xFFFFU, 0xFFFFU});

    cpu.step(bus);
    cpu.step(bus);

    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
    EXPECT_EQ(cpu.D(0), 0xFFFFFFFFU);
}

TEST_F(CpuTest, CmpWordProgramCounterDisplacement) {
    load_program({
        0x7020U, // MOVEQ #32, D0
        0xB07AU, // CMP.W d16(PC), D0
        0x0004U, // displacement +4 (target = 0x1004 + 4 = 0x1008)
        0x4E71U, // NOP (0x1006)
        0x0020U  // data word 32 (0x1008)
    });

    cpu.step(bus); // MOVEQ

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.W

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpByteProgramCounterDisplacementNegative) {
    bus.poke8(0x0FFEU, 0x55U);
    load_program({
        0x7055U, // MOVEQ #0x55, D0
        0xB03AU, // CMP.B d16(PC), D0
        0xFFFAU  // displacement -6 (target = 0x1004 - 6 = 0x0FFE)
    });

    cpu.step(bus); // MOVEQ

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.B

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpWordProgramCounterIndexWordSigned) {
    load_program({
        0x7020U, // MOVEQ #32, D0
        0x7204U, // MOVEQ #4, D1
        0xB07BU, // CMP.W d8(PC, D1.W), D0
        0x1002U, // extension: D1.W + 2 (target = 0x1006 + 4 + 2 = 0x100C)
        0x0000U, // 0x1008
        0x0000U, // 0x100A
        0x0020U  // data word 32 (0x100C)
    });

    cpu.step(bus); // MOVEQ D0
    cpu.step(bus); // MOVEQ D1

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.W

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpWordProgramCounterIndexWordNegativeSignExtended) {
    load_program({
        0x7010U, // MOVEQ #16, D0
        0x7200U, // MOVEQ #0, D1
        0x5941U, // SUBQ.W #4, D1 -> D1 = 0x0000FFFC (low word is -4)
        0xB07BU, // CMP.W d8(PC, D1.W), D0
        0x1006U, // extension: D1.W + 6 (target = 0x1008 + (-4) + 6 = 0x100A)
        0x0010U  // data word 16 (0x100A)
    });

    cpu.step(bus); // MOVEQ D0
    cpu.step(bus); // MOVEQ D1
    cpu.step(bus); // SUBQ.W D1

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.W

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpLongProgramCounterIndexLong) {
    bus.poke16(0x1012U, 0x1234U);
    bus.poke16(0x1014U, 0x5678U);
    load_program({
        0x5089U, // ADDQ.L #8, A1 -> A1 = 8
        0x7000U, // MOVEQ #0, D0
        0xB0BBU, // CMP.L d8(PC, A1.L), D0
        0x9804U  // extension: A1.L + 4 (target = 0x1006 + 8 + 4 = 0x1012)
    });

    cpu.step(bus); // ADDQ.L A1
    cpu.step(bus); // MOVEQ D0

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.L

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_FALSE(flag_z());
    EXPECT_TRUE(flag_n());
    EXPECT_TRUE(flag_c()); // 0 < 0x12345678 -> borrow
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, CmpByteProgramCounterIndexNegativeDisplacement) {
    bus.poke8(0x0FFEU, 42U);
    load_program({
        0x702AU, // MOVEQ #42, D0
        0x7200U, // MOVEQ #0, D1
        0xB03BU, // CMP.B d8(PC, D1.W), D0
        0x10F8U  // extension: D1.W + (-8) (target = 0x1006 + 0 - 8 = 0x0FFE)
    });

    cpu.step(bus); // MOVEQ D0
    cpu.step(bus); // MOVEQ D1

    const auto initial_pc = cpu.pc();
    cpu.step(bus); // CMP.B

    EXPECT_EQ(cpu.pc(), initial_pc + 4U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_c());
    EXPECT_FALSE(flag_v());
}

TEST_F(CpuTest, JmpAddressRegisterIndirect) {
    load_program({
        0x5488U, // ADDQ.L #2, A0
        0x4ED0U  // JMP (A0)
    });

    cpu.step(bus); // ADDQ.L
    EXPECT_EQ(cpu.A(0), 2U);

    cpu.step(bus); // JMP (A0)
    EXPECT_EQ(cpu.pc(), 2U);
    EXPECT_EQ(cpu.A(0), 2U);
}

TEST_F(CpuTest, JmpAddressRegisterDisplacement) {
    load_program({
        0x5888U, // ADDQ.L #4, A0
        0x4EE8U, // JMP 16(A0)
        0x0010U  // displacement +16
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // JMP 16(A0)
    EXPECT_EQ(cpu.pc(), 20U);
}

TEST_F(CpuTest, JmpAddressRegisterIndex) {
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7210U, // MOVEQ #16, D1
        0x4EF0U, // JMP 4(A0, D1.W)
        0x1004U  // extension: D1.W, disp +4
    });

    cpu.step(bus);            // ADDQ.L
    cpu.step(bus);            // MOVEQ
    cpu.step(bus);            // JMP
    EXPECT_EQ(cpu.pc(), 28U); // 8 + 16 + 4
}

TEST_F(CpuTest, JmpAbsoluteShort) {
    load_program({
        0x4EF8U, // JMP ($3000).W
        0x3000U  // address
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x3000U);
}

TEST_F(CpuTest, JmpAbsoluteLong) {
    load_program({
        0x4EF9U, // JMP ($00045678).L
        0x0004U, // high word
        0x5678U  // low word
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x00045678U);
}

TEST_F(CpuTest, JmpProgramCounterDisplacement) {
    load_program({
        0x4EFAU, // JMP d16(PC)
        0x0020U  // displacement +32 (relative to 0x1002)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x1022U);
}

TEST_F(CpuTest, JmpProgramCounterIndex) {
    load_program({
        0x7204U, // MOVEQ #4, D1
        0x4EFBU, // JMP d8(PC, D1.W)
        0x1006U  // extension: D1.W, disp +6 (relative to 0x1004)
    });

    cpu.step(bus);                // MOVEQ
    cpu.step(bus);                // JMP
    EXPECT_EQ(cpu.pc(), 0x100EU); // 0x1004 + 4 + 6
}

TEST_F(CpuTest, JmpPreservesConditionCodes) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 (sets N flag, clears Z, V, C)
        0x4ED0U  // JMP (A0)
    });

    cpu.step(bus); // MOVEQ
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());

    cpu.step(bus); // JMP (A0)
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, JmpDataRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4EC0U // JMP D0 (mode 0, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JmpAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4EC8U // JMP A0 (mode 1, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JmpPostIncrementThrowsUnsupportedInstruction) {
    load_program({
        0x4ED8U // JMP (A0)+ (mode 3, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JmpPreDecrementThrowsUnsupportedInstruction) {
    load_program({
        0x4EE0U // JMP -(A0) (mode 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JmpImmediateThrowsUnsupportedInstruction) {
    load_program({
        0x4EFCU // JMP #<data> (mode 7, reg 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JsrAddressRegisterIndirect) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x5488U, // ADDQ.L #2, A0
        0x4E90U  // JSR (A0)
    });

    cpu.step(bus); // ADDQ.L
    EXPECT_EQ(cpu.A(0), 2U);

    cpu.step(bus); // JSR (A0)
    EXPECT_EQ(cpu.pc(), 2U);
    EXPECT_EQ(cpu.A(0), 2U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);

    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 4U);
}

TEST_F(CpuTest, JsrAddressRegisterDisplacement) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x5888U, // ADDQ.L #4, A0
        0x4EA8U, // JSR 16(A0) (2 words: opcode + disp)
        0x0010U  // displacement +16
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // JSR 16(A0)
    EXPECT_EQ(cpu.pc(), 20U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    // Multi-word return address must point past the displacement word
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 6U);
}

TEST_F(CpuTest, JsrAddressRegisterIndex) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7210U, // MOVEQ #16, D1
        0x4EB0U, // JSR 4(A0, D1.W)
        0x1004U  // extension: D1.W, disp +4
    });

    cpu.step(bus);            // ADDQ.L
    cpu.step(bus);            // MOVEQ
    cpu.step(bus);            // JSR
    EXPECT_EQ(cpu.pc(), 28U); // 8 + 16 + 4
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 8U);
}

TEST_F(CpuTest, JsrAbsoluteShort) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x4EB8U, // JSR ($3000).W
        0x3000U  // address
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x3000U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 4U);
}

TEST_F(CpuTest, JsrAbsoluteLong) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x4EB9U, // JSR ($00045678).L (3 words: opcode + 2 address words)
        0x0004U, // high word
        0x5678U  // low word
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x00045678U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    // 3-word instruction pushes return address after full 6-byte instruction
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 6U);
}

TEST_F(CpuTest, JsrProgramCounterDisplacement) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x4EBAU, // JSR d16(PC)
        0x0020U  // displacement +32 (relative to extension word at 0x1002)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x1022U);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 4U);
}

TEST_F(CpuTest, JsrProgramCounterIndex) {
    const auto initial_sp = cpu.A(7);

    load_program({
        0x7204U, // MOVEQ #4, D1
        0x4EBBU, // JSR d8(PC, D1.W)
        0x1006U  // extension: D1.W, disp +6 (relative to 0x1004)
    });

    cpu.step(bus);                // MOVEQ
    cpu.step(bus);                // JSR
    EXPECT_EQ(cpu.pc(), 0x100EU); // 0x1004 + 4 + 6
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), kDefaultPc + 6U);
}

TEST_F(CpuTest, JsrPreservesConditionCodes) {
    load_program({
        0x70FFU, // MOVEQ #-1, D0 (sets N flag, clears Z, V, C)
        0x4E90U  // JSR (A0)
    });

    cpu.step(bus); // MOVEQ
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());

    cpu.step(bus); // JSR (A0)
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, JsrDataRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4E80U // JSR D0 (mode 0, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JsrAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4E88U // JSR A0 (mode 1, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JsrPostIncrementThrowsUnsupportedInstruction) {
    load_program({
        0x4E98U // JSR (A0)+ (mode 3, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JsrPreDecrementThrowsUnsupportedInstruction) {
    load_program({
        0x4EA0U // JSR -(A0) (mode 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, JsrImmediateThrowsUnsupportedInstruction) {
    load_program({
        0x4EBCU // JSR #<data> (mode 7, reg 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, LeaAddressRegisterIndirect) {
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x43D0U  // LEA (A0), A1
    });

    cpu.step(bus); // ADDQ.L
    EXPECT_EQ(cpu.A(0), 8U);

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus); // LEA (A0), A1
    EXPECT_EQ(cpu.A(1), 8U);
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);

    // Verify LEA does not read from or write to the target effective address
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U); // only opcode fetched
    EXPECT_TRUE(bus.writes.empty());
}

TEST_F(CpuTest, LeaAddressRegisterDisplacement) {
    load_program({
        0x5888U, // ADDQ.L #4, A0
        0x43E8U, // LEA 16(A0), A1
        0x0010U  // displacement +16
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // LEA 16(A0), A1
    EXPECT_EQ(cpu.A(1), 20U);
    EXPECT_EQ(cpu.A(0), 4U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, LeaAddressRegisterDisplacementSameRegister) {
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x41E8U, // LEA 4(A0), A0 (in-place advance)
        0x0004U  // displacement +4
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // LEA 4(A0), A0
    EXPECT_EQ(cpu.A(0), 12U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, LeaAddressRegisterIndex) {
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7210U, // MOVEQ #16, D1
        0x45F0U, // LEA 4(A0, D1.W), A2
        0x1004U  // extension: D1.W, disp +4
    });

    cpu.step(bus);            // ADDQ.L
    cpu.step(bus);            // MOVEQ
    cpu.step(bus);            // LEA
    EXPECT_EQ(cpu.A(2), 28U); // 8 + 16 + 4
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.D(1), 16U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, LeaAbsoluteShort) {
    load_program({
        0x41F8U, // LEA ($3000).W, A0
        0x3000U  // address
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 0x3000U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, LeaAbsoluteLong) {
    load_program({
        0x41F9U, // LEA ($00045678).L, A0
        0x0004U, // high word
        0x5678U  // low word
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 0x00045678U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, LeaProgramCounterDisplacement) {
    load_program({
        0x41FAU, // LEA d16(PC), A0
        0x0020U  // displacement +32 (relative to 0x1002)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(0), 0x1022U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, LeaProgramCounterIndex) {
    load_program({
        0x7204U, // MOVEQ #4, D1
        0x41FBU, // LEA d8(PC, D1.W), A0
        0x1006U  // extension: D1.W, disp +6 (relative to 0x1004)
    });

    cpu.step(bus);                // MOVEQ
    cpu.step(bus);                // LEA
    EXPECT_EQ(cpu.A(0), 0x100EU); // 0x1004 + 4 + 6
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, LeaPreservesConditionCodes) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags)
        0x43D0U  // LEA (A0), A1
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SUBQ
    EXPECT_TRUE(flag_x());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_TRUE(flag_c());

    cpu.step(bus); // LEA (A0), A1
    EXPECT_TRUE(flag_x());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_TRUE(flag_c());
}

TEST_F(CpuTest, LeaDataRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x41C0U // LEA D0, A0 (mode 0, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, LeaAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x41C8U // LEA A0, A0 (mode 1, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, LeaPostIncrementThrowsUnsupportedInstruction) {
    load_program({
        0x41D8U // LEA (A0)+, A0 (mode 3, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, LeaPreDecrementThrowsUnsupportedInstruction) {
    load_program({
        0x41E0U // LEA -(A0), A0 (mode 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, LeaImmediateThrowsUnsupportedInstruction) {
    load_program({
        0x41FCU // LEA #<data>, A0 (mode 7, reg 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, PeaAddressRegisterIndirect) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x4850U  // PEA (A0)
    });

    cpu.step(bus); // ADDQ.L
    EXPECT_EQ(cpu.A(0), 8U);

    bus.reads.clear();
    bus.writes.clear();

    cpu.step(bus); // PEA (A0)
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 8U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);

    // Verify PEA fetched opcode, wrote to stack, but did not read from the
    // effective address
    ASSERT_EQ(bus.reads.size(), 1U);
    EXPECT_EQ(bus.reads[0].address, kDefaultPc + 2U);
    ASSERT_EQ(bus.writes.size(), 2U); // 32-bit push via two 16-bit writes
    EXPECT_EQ(bus.peek32(cpu.A(7)), 8U);
}

TEST_F(CpuTest, PeaAddressRegisterDisplacement) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x5888U, // ADDQ.L #4, A0
        0x4868U, // PEA 16(A0)
        0x0010U  // displacement +16
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // PEA 16(A0)
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 20U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, PeaAddressRegisterDisplacementUsingStackPointer) {
    // Verifies effective address uses SP before it gets decremented
    const auto initial_sp = cpu.A(7);
    load_program({
        0x486FU, // PEA 8(A7)
        0x0008U  // displacement +8
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), initial_sp + 8U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, PeaAddressRegisterIndirectUsingStackPointer) {
    // Verifies effective address uses SP before it gets decremented
    const auto initial_sp = cpu.A(7);
    load_program({
        0x4857U // PEA (A7)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), initial_sp);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 2U);
}

TEST_F(CpuTest, PeaAddressRegisterIndex) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x5088U, // ADDQ.L #8, A0
        0x7210U, // MOVEQ #16, D1
        0x4870U, // PEA 4(A0, D1.W)
        0x1004U  // extension: D1.W, disp +4
    });

    cpu.step(bus); // ADDQ.L
    cpu.step(bus); // MOVEQ
    cpu.step(bus); // PEA
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 28U); // 8 + 16 + 4
    EXPECT_EQ(cpu.A(0), 8U);
    EXPECT_EQ(cpu.D(1), 16U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 8U);
}

TEST_F(CpuTest, PeaAbsoluteShort) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x4878U, // PEA ($3000).W
        0x3000U  // address
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 0x3000U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, PeaAbsoluteLong) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x4879U, // PEA ($00045678).L
        0x0004U, // high word
        0x5678U  // low word
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 0x00045678U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, PeaProgramCounterDisplacement) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x487AU, // PEA d16(PC)
        0x0020U  // displacement +32 (relative to 0x1002)
    });

    cpu.step(bus);
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 0x1022U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
}

TEST_F(CpuTest, PeaProgramCounterIndex) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x7204U, // MOVEQ #4, D1
        0x487BU, // PEA d8(PC, D1.W)
        0x1006U  // extension: D1.W, disp +6 (relative to 0x1004)
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // PEA
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_EQ(bus.peek32(cpu.A(7)), 0x100EU); // 0x1004 + 4 + 6
    EXPECT_EQ(cpu.pc(), kDefaultPc + 6U);
}

TEST_F(CpuTest, PeaPreservesConditionCodes) {
    const auto initial_sp = cpu.A(7);
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags)
        0x4850U  // PEA (A0)
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SUBQ
    EXPECT_TRUE(flag_x());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_TRUE(flag_c());

    cpu.step(bus); // PEA (A0)
    EXPECT_EQ(cpu.A(7), initial_sp - 4U);
    EXPECT_TRUE(flag_x());
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_TRUE(flag_c());
}

TEST_F(CpuTest, PeaAddressRegisterDirectThrowsUnsupportedInstruction) {
    load_program({
        0x4848U // PEA A0 (mode 1, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, PeaPostIncrementThrowsUnsupportedInstruction) {
    load_program({
        0x4858U // PEA (A0)+ (mode 3, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, PeaPreDecrementThrowsUnsupportedInstruction) {
    load_program({
        0x4860U // PEA -(A0) (mode 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, PeaImmediateThrowsUnsupportedInstruction) {
    load_program({
        0x487CU // PEA #<data> (mode 7, reg 4, non-control)
    });

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST_F(CpuTest, SwapWordHalvesPositive) {
    load_program({
        0x7001U, // MOVEQ #1, D0 -> D0 = 0x00000001
        0x4840U  // SWAP D0
    });

    cpu.step(bus); // MOVEQ
    EXPECT_EQ(cpu.D(0), 1U);

    cpu.step(bus); // SWAP D0
    EXPECT_EQ(cpu.D(0), 0x00010000U);
    EXPECT_EQ(cpu.pc(), kDefaultPc + 4U);
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SwapConsecutiveRestoresValue) {
    load_program({
        0x7080U, // MOVEQ #-128, D0 -> D0 = 0xFFFFFF80
        0x4840U, // SWAP D0 -> D0 = 0xFF80FFFF
        0x4840U  // SWAP D0 -> D0 = 0xFFFFFF80
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SWAP D0
    EXPECT_EQ(cpu.D(0), 0xFF80FFFFU);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());

    cpu.step(bus); // SWAP D0
    EXPECT_EQ(cpu.D(0), 0xFFFFFF80U);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SwapZeroSetsZeroFlag) {
    load_program({
        0x7000U, // MOVEQ #0, D0 -> D0 = 0
        0x4840U  // SWAP D0
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SWAP D0
    EXPECT_EQ(cpu.D(0), 0U);
    EXPECT_TRUE(flag_z());
    EXPECT_FALSE(flag_n());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SwapNegativeSetsNegativeFlag) {
    load_program({
        0x7080U, // MOVEQ #-128, D0 -> D0 = 0xFFFFFF80
        0x4840U  // SWAP D0 -> 0xFF80FFFF (bit 31 is 1)
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SWAP D0
    EXPECT_EQ(cpu.D(0), 0xFF80FFFFU);
    EXPECT_TRUE(flag_n());
    EXPECT_FALSE(flag_z());
    EXPECT_FALSE(flag_v());
    EXPECT_FALSE(flag_c());
}

TEST_F(CpuTest, SwapPreservesExtendFlagAndClearsOverflowAndCarry) {
    load_program({
        0x7000U, // MOVEQ #0, D0
        0x5380U, // SUBQ.L #1, D0 (sets X, N, C flags; clears Z, V)
        0x4840U  // SWAP D0
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // SUBQ
    EXPECT_TRUE(flag_x());
    EXPECT_TRUE(flag_c());

    cpu.step(bus);          // SWAP D0
    EXPECT_TRUE(flag_x());  // X preserved
    EXPECT_FALSE(flag_c()); // C cleared
    EXPECT_FALSE(flag_v()); // V cleared
    EXPECT_TRUE(flag_n());  // N set (0xFFFFFFFF)
    EXPECT_FALSE(flag_z());
}

TEST_F(CpuTest, SwapClearsOverflowFlag) {
    load_program({
        0x707FU, // MOVEQ #127, D0
        0x5200U, // ADDQ.B #1, D0 (127 + 1 = 128 -> overflow V = 1)
        0x4840U  // SWAP D0
    });

    cpu.step(bus); // MOVEQ
    cpu.step(bus); // ADDQ.B
    EXPECT_TRUE(flag_v());

    cpu.step(bus);          // SWAP D0
    EXPECT_FALSE(flag_v()); // V cleared
}

TEST_F(CpuTest, SwapDifferentRegisters) {
    load_program({
        0x762AU, // MOVEQ #42, D3
        0x7E07U, // MOVEQ #7, D7
        0x4843U, // SWAP D3
        0x4847U  // SWAP D7
    });

    cpu.step(bus); // MOVEQ D3
    cpu.step(bus); // MOVEQ D7
    cpu.step(bus); // SWAP D3
    EXPECT_EQ(cpu.D(3), 0x002A0000U);
    cpu.step(bus); // SWAP D7
    EXPECT_EQ(cpu.D(7), 0x00070000U);
}
