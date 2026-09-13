#include "m68000/cpu.hpp"

#include "m68000/bus.hpp"

namespace m68000 {

namespace {
constexpr std::uint16_t supervisor_mask = 1U << 13;
constexpr std::uint16_t interrupt_mask = 0x0700;

constexpr std::uint16_t nop_opcode = 0x4E71U;
} // namespace

void Cpu::reset(Bus &bus) {
    A_[7] = bus.read16(0x000000);
    A_[7] <<= 16;
    A_[7] |= bus.read16(0x000002);

    pc_ = bus.read16(0x000004);
    pc_ <<= 16;
    pc_ |= bus.read16(0x000006);

    status_ = supervisor_mask | interrupt_mask;
}

void Cpu::step(Bus &bus) {
    auto instr = bus.read16(pc_);
    pc_ += 2;

    if (instr == nop_opcode) {
        return;
    }

    throw UnsupportedInstruction{instr};
}

} // namespace m68000
