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
 * Bcc / BRA
 *
 * Bcc <label>
 * BRA <label>
 *
 * 15             12 11    8 7                  0
 * +----------------+--------+--------------------+
 * |      0110      |  cond  | 8-bit displacement |
 * +----------------+--------+--------------------+
 * |       16-bit displacement if 8-bit is 0      |
 * +----------------------------------------------+
 *
 * X — Not affected.
 * N — Not affected.
 * Z — Not affected.
 * V — Not affected.
 * C — Not affected.
 */

constexpr std::uint16_t branch_mask = 0xF000U;
constexpr std::uint16_t branch_pattern = 0x6000U;

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
    } else if ((instr & branch_mask) == branch_pattern) {
        std::uint8_t condition{
            static_cast<std::uint8_t>((instr >> 8U) & 0x0FU)};

        // 0x61xx is BSR (Branch to Subroutine)
        if (condition == 1) {
            throw UnsupportedInstruction{instr};
        }

        std::int32_t displacement{static_cast<std::int8_t>(instr & 0x00FFU)};
        if (condition != 0 && !condition_true(condition)) {
            if (displacement == 0) {
                bus.read16(pc_);
                pc_ += 2;
            }
            return;
        }

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

bool Cpu::condition_true(std::uint8_t condition) const {
    const bool c = (status_ & carry_flag) != 0;
    const bool v = (status_ & overflow_flag) != 0;
    const bool z = (status_ & zero_flag) != 0;
    const bool n = (status_ & negative_flag) != 0;

    switch (condition) {
    case 2: // HI: High (!C && !Z)
        return !c && !z;
    case 3: // LS: Low or Same (C || Z)
        return c || z;
    case 4: // CC/HS: Carry Clear / High or Same (!C)
        return !c;
    case 5: // CS/LO: Carry Set / Low (C)
        return c;
    case 6: // NE: Not Equal (!Z)
        return !z;
    case 7: // EQ: Equal (Z)
        return z;
    case 8: // VC: Overflow Clear (!V)
        return !v;
    case 9: // VS: Overflow Set (V)
        return v;
    case 10: // PL: Plus / Positive (!N)
        return !n;
    case 11: // MI: Minus / Negative (N)
        return n;
    case 12: // GE: Greater or Equal (N == V)
        return n == v;
    case 13: // LT: Less Than (N != V)
        return n != v;
    case 14: // GT: Greater Than (!Z && N == V)
        return !z && (n == v);
    case 15: // LE: Less or Equal (Z || N != V)
        return z || (n != v);
    default:
        throw UnsupportedCondition{condition};
    }

    return false;
}

} // namespace m68000
