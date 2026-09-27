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

    [[nodiscard]] std::uint8_t peek8(std::uint32_t address) const {
        return load_byte(address);
    }

    [[nodiscard]] std::uint16_t peek16(std::uint32_t address) const {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint32_t>(load_byte(address)) << 8) |
            static_cast<std::uint32_t>(load_byte(address + 1U)));
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
        bus.poke16(0x000000U, static_cast<std::uint16_t>(kDefaultSp >> 16));
        bus.poke16(0x000002U, static_cast<std::uint16_t>(kDefaultSp & 0xFFFFU));
        bus.poke16(0x000004U, static_cast<std::uint16_t>(kDefaultPc >> 16));
        bus.poke16(0x000006U, static_cast<std::uint16_t>(kDefaultPc & 0xFFFFU));
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
    bus.poke16(initial_sp, static_cast<std::uint16_t>(target_pc >> 16));
    bus.poke16(initial_sp + 2U,
               static_cast<std::uint16_t>(target_pc & 0xFFFFU));

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
        0x5270U // ADDQ.W #1, 0(A0, D0) (mode 6, unsupported)
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
