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
    std::map<std::uint32_t, std::uint16_t> memory16;
    std::map<std::uint32_t, std::uint8_t> memory8;

    std::uint8_t read8(std::uint32_t address) override {
        reads.push_back({address, 8});
        auto it = memory8.find(address);
        return (it != memory8.end()) ? it->second : std::uint8_t{0};
    }

    std::uint16_t read16(std::uint32_t address) override {
        reads.push_back({address, 16});
        auto it = memory16.find(address);
        return (it != memory16.end()) ? it->second : std::uint16_t{0};
    }

    void write8(std::uint32_t address, std::uint8_t value) override {
        writes.push_back({address, static_cast<std::uint16_t>(value), 8});
        memory8[address] = value;
    }

    void write16(std::uint32_t address, std::uint16_t value) override {
        writes.push_back({address, value, 16});
        memory16[address] = value;
    }
};

class CpuTest : public ::testing::Test {
protected:
    static constexpr std::uint32_t kDefaultPc = 0x00001000U;
    static constexpr std::uint32_t kDefaultSp = 0x00200000U;

    FakeBus bus;
    m68000::Cpu cpu;

    void SetUp() override {
        bus.memory16[0x000000U] = static_cast<std::uint16_t>(kDefaultSp >> 16);
        bus.memory16[0x000002U] =
            static_cast<std::uint16_t>(kDefaultSp & 0xFFFFU);
        bus.memory16[0x000004U] = static_cast<std::uint16_t>(kDefaultPc >> 16);
        bus.memory16[0x000006U] =
            static_cast<std::uint16_t>(kDefaultPc & 0xFFFFU);
        cpu.reset(bus);
        bus.reads.clear();
        bus.writes.clear();
    }

    void load_program(std::initializer_list<std::uint16_t> opcodes,
                      std::uint32_t address = kDefaultPc) {
        for (std::uint16_t op : opcodes) {
            bus.memory16[address] = op;
            address += 2U;
        }
    }

    [[nodiscard]] bool flag_c() const noexcept {
        return (cpu.status() & 0x0001U) != 0;
    }

    [[nodiscard]] bool flag_v() const noexcept {
        return (cpu.status() & 0x0002U) != 0;
    }

    [[nodiscard]] bool flag_z() const noexcept {
        return (cpu.status() & 0x0004U) != 0;
    }

    [[nodiscard]] bool flag_n() const noexcept {
        return (cpu.status() & 0x0008U) != 0;
    }

    [[nodiscard]] bool flag_x() const noexcept {
        return (cpu.status() & 0x0010U) != 0;
    }

    [[nodiscard]] std::uint16_t flags_nzvc() const noexcept {
        return static_cast<std::uint16_t>(cpu.status() & 0x000FU);
    }
};

} // namespace

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
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x4000U;
    bus.memory16[0x000004U] = 0x0001U;
    bus.memory16[0x000006U] = 0x8000U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_EQ(cpu.A(7), 0x00204000U);
    EXPECT_EQ(cpu.pc(), 0x00018000U);
    EXPECT_EQ(cpu.status(), 0x2700U);
}

TEST(CpuResetTest, ResetHandlesFull32BitRangeWithoutSignExtension) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0xFFFFU;
    bus.memory16[0x000002U] = 0xFFFFU;
    bus.memory16[0x000004U] = 0x8000U;
    bus.memory16[0x000006U] = 0x0001U;

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
