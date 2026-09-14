#include "m68000/bus.hpp"
#include "m68000/cpu.hpp"

#include <cstdint>
#include <gtest/gtest.h>
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

} // namespace

TEST(CpuTest, CanBeConstructed) {
    [[maybe_unused]] m68000::Cpu cpu;
    SUCCEED();
}

TEST(CpuTest, ResetReadsVectorTableInCorrectOrder) {
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

TEST(CpuTest, ResetDoesNotPerformAnyBusWrites) {
    FakeBus bus;
    m68000::Cpu cpu;

    cpu.reset(bus);

    EXPECT_TRUE(bus.writes.empty());
}

TEST(CpuTest, ResetSetsRegistersToLoadedValues) {
    FakeBus bus;
    // Initial SSP: 0x00204000
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x4000U;

    // Initial PC: 0x00018000
    bus.memory16[0x000004U] = 0x0001U;
    bus.memory16[0x000006U] = 0x8000U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_EQ(cpu.A(7), 0x00204000U);
    EXPECT_EQ(cpu.pc(), 0x00018000U);
    EXPECT_EQ(cpu.status(), 0x2700U);
}

TEST(CpuTest, ResetHandlesFull32BitRangeWithoutSignExtension) {
    FakeBus bus;
    // Test high bit patterns across words
    bus.memory16[0x000000U] = 0xFFFFU;
    bus.memory16[0x000002U] = 0xFFFFU;
    bus.memory16[0x000004U] = 0x8000U;
    bus.memory16[0x000006U] = 0x0001U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_EQ(cpu.A(7), 0xFFFFFFFFU);
    EXPECT_EQ(cpu.pc(), 0x80000001U);
}

TEST(CpuTest, StepExecutesNopAndIncrementsPc) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    // NOP opcode: 0x4E71
    bus.memory16[0x00001000U] = 0x4E71U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    const auto pre_reads = bus.reads.size();
    const auto initial_status = cpu.status();
    const auto initial_sp = cpu.A(7);

    cpu.step(bus);

    EXPECT_EQ(cpu.pc(), 0x00001002U);
    EXPECT_EQ(cpu.status(), initial_status);
    EXPECT_EQ(cpu.A(7), initial_sp);
    EXPECT_EQ(bus.reads.size(), pre_reads + 1U);
    EXPECT_EQ(bus.reads.back().address, 0x00001000U);
    EXPECT_EQ(bus.reads.back().size, 16U);
    EXPECT_TRUE(bus.writes.empty());
}

TEST(CpuTest, StepExecutesMultipleNopsSequentially) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    bus.memory16[0x00001000U] = 0x4E71U;
    bus.memory16[0x00001002U] = 0x4E71U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x00001002U);

    cpu.step(bus);
    EXPECT_EQ(cpu.pc(), 0x00001004U);
}

TEST(CpuTest, StepThrowsOnUnsupportedInstruction) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    // Arbitrary unhandled opcode
    bus.memory16[0x00001000U] = 0x1234U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    EXPECT_THROW(cpu.step(bus), m68000::UnsupportedInstruction);
}

TEST(CpuTest, MoveqLoadsPositiveImmediateAndClearsFlags) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    // MOVEQ #42, D3 -> 0x762A
    bus.memory16[0x00001000U] = 0x762AU;

    m68000::Cpu cpu;
    cpu.reset(bus);

    cpu.step(bus);

    EXPECT_EQ(cpu.D(3), 42U);
    EXPECT_EQ(cpu.pc(), 0x00001002U);
    // N=0, Z=0, V=0, C=0 (bits 3..0 of status should be 0)
    EXPECT_EQ(cpu.status() & 0x000FU, 0x0000U);
}

TEST(CpuTest, MoveqSignExtendsNegativeImmediateAndSetsNegativeFlag) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    // MOVEQ #-1, D0 -> 0x70FF (0xFF sign-extended to 32 bits is 0xFFFFFFFF)
    bus.memory16[0x00001000U] = 0x70FFU;

    m68000::Cpu cpu;
    cpu.reset(bus);

    cpu.step(bus);

    EXPECT_EQ(cpu.D(0), 0xFFFFFFFFU);
    EXPECT_EQ(cpu.pc(), 0x00001002U);
    // N flag is bit 3 (0x0008), Z=0, V=0, C=0
    EXPECT_EQ(cpu.status() & 0x000FU, 0x0008U);
}

TEST(CpuTest, MoveqSetsZeroFlagWhenImmediateIsZero) {
    FakeBus bus;
    bus.memory16[0x000000U] = 0x0020U;
    bus.memory16[0x000002U] = 0x0000U;
    bus.memory16[0x000004U] = 0x0000U;
    bus.memory16[0x000006U] = 0x1000U;

    // MOVEQ #0, D5 -> 0x7A00
    bus.memory16[0x00001000U] = 0x7A00U;

    m68000::Cpu cpu;
    cpu.reset(bus);

    cpu.step(bus);

    EXPECT_EQ(cpu.D(5), 0U);
    EXPECT_EQ(cpu.pc(), 0x00001002U);
    // Z flag is bit 2 (0x0004), N=0, V=0, C=0
    EXPECT_EQ(cpu.status() & 0x000FU, 0x0004U);
}
