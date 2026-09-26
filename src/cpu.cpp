#include "m68000/cpu.hpp"

#include "m68000/bus.hpp"

namespace m68000 {

namespace {

/*
 * Status Register (SR)
 *
 *  15  14  13  12  11  10   9   8    7   6   5   4   3   2   1   0
 * +---+---+---+---+---+---+---+---+  +---+---+---+---+---+---+---+---+
 * | T | 0 | S | 0 | 0 |I2 |I1 |I0 |  | 0 | 0 | 0 | X | N | Z | V | C |
 * +---+---+---+---+---+---+---+---+  +---+---+---+---+---+---+---+---+
 * |         System byte           |  |           CCR                 |
 * +-------------------------------+  +-------------------------------+
 *
 * X — Extend
 * N — Negative
 * Z — Zero
 * V — Overflow
 * C — Carry
 */

constexpr std::uint16_t supervisor_mask = 1U << 13;
constexpr std::uint16_t interrupt_mask = 0x0700;

constexpr std::uint16_t nop_opcode = 0x4E71U;

/*
 * MOVEQ
 *
 * MOVEQ #5, D2
 *
 * 15             12 11    9 8 7                  0
 * +----------------+--------+-+--------------------+
 * |     0111       |  Dn    |0|      immediate     |
 * +----------------+--------+-+--------------------+
 *
 * X — Not affected.
 * N — Set if the result is negative; cleared otherwise.
 * Z — Set if the result is zero; cleared otherwise.
 * V — Always cleared.
 * C — Always cleared.
 */

constexpr std::uint16_t moveq_mask = 0xF100U;
constexpr std::uint16_t moveq_pattern = 0x7000U;
/*
 * BRA
 *
 * BRA <label>
 *
 * 15                         8 7                  0
 * +---------------------------+--------------------+
 * |         01100000          | 8-bit displacement |
 * +---------------------------+--------------------+
 * |        16-bit displacement if 8-bit is 0       |
 * +------------------------------------------------+
 *
 * X — Not affected.
 * N — Not affected.
 * Z — Not affected.
 * V — Not affected.
 * C — Not affected.
 */

constexpr std::uint16_t bra_mask = 0xFF00U;
constexpr std::uint16_t bra_pattern = 0x6000U;

[[nodiscard]] constexpr std::uint32_t
add_displacement(std::uint32_t address, std::int32_t displacement) noexcept {
    return address + static_cast<std::uint32_t>(displacement);
}

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
    std::uint16_t instr = bus.read16(pc_);
    pc_ += 2U;

    if (instr == nop_opcode) {
        return;
    } else if ((instr & moveq_mask) == moveq_pattern) {
        std::size_t index{(instr >> 9) & 0x0007U};
        std::int32_t immediate{static_cast<std::int8_t>(instr & 0x00FFU)};
        D_[index] = static_cast<std::uint32_t>(immediate);
        status_ &= static_cast<std::uint16_t>(~nzvc_flags);
        if (immediate == 0) {
            status_ |= zero_flag;
        } else if (immediate < 0) {
            status_ |= negative_flag;
        }
        return;
    } else if ((instr & bra_mask) == bra_pattern) {
        std::int32_t displacement{static_cast<std::int8_t>(instr & 0x00FFU)};
        if (displacement == 0) {
            // 16-bit displacement is relative to current pc_
            std::uint16_t displacement16 = bus.read16(pc_);
            displacement = static_cast<std::int16_t>(displacement16);
        }
        pc_ = add_displacement(pc_, displacement);
        return;
    }

    throw UnsupportedInstruction{instr};
}

} // namespace m68000
