#include "m68000/cpu.hpp"

#include "m68000/bus.hpp"

#include <cassert>

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
 * RTS
 *
 * RTS
 *
 * 15                                              0
 * +-----------------------------------------------+
 * |               0100111001110101                |
 * +-----------------------------------------------+
 *
 * X — Not affected.
 * N — Not affected.
 * Z — Not affected.
 * V — Not affected.
 * C — Not affected.
 */
constexpr std::uint16_t rts_opcode = 0x4E75U;

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
 * Bcc / BRA / BSR
 *
 * Bcc <label>
 * BRA <label>
 * BSR <label>
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

/*
 * ADDQ / SUBQ
 *
 * ADDQ #<data>, <ea>
 * SUBQ #<data>, <ea>
 *
 * 15             12 11    9 8   7   6 5            0
 * +----------------+-------+---+-----+--------------+
 * |      0101      | data  |o/s| size|      ea      |
 * +----------------+-------+---+-----+--------------+
 *
 * data: 1-7 represents 1-7; 0 represents 8.
 * o/s: 0 = ADDQ, 1 = SUBQ.
 * size: 00 = Byte, 01 = Word, 10 = Long.
 *
 * For Dn and memory:
 * X — Set to the value of the carry bit.
 * N — Set if the result is negative; cleared otherwise.
 * Z — Set if the result is zero; cleared otherwise.
 * V — Set if an overflow occurs; cleared otherwise.
 * C — Set if a carry/borrow occurs; cleared otherwise.
 *
 * For An:
 * Condition codes are not affected.
 */

constexpr std::uint16_t addq_subq_mask = 0xF000U;
constexpr std::uint16_t addq_subq_pattern = 0x5000U;

[[nodiscard]] constexpr std::uint32_t
add_displacement(std::uint32_t address, std::int32_t displacement) noexcept {
    return address + static_cast<std::uint32_t>(displacement);
}

[[nodiscard]] constexpr std::uint32_t get_size_mask(Cpu::OperandSize size) {
    switch (size) {
        case Cpu::OperandSize::byte: return 0x000000FF;
        case Cpu::OperandSize::word: return 0x0000FFFF;
        case Cpu::OperandSize::long_word: return 0xFFFFFFFF;
    }
    return 0x0;
}

[[nodiscard]] Cpu::OperandSize decode_size(std::uint8_t size) {
    switch (size) {
        case 0: return Cpu::OperandSize::byte;
        case 1: return Cpu::OperandSize::word;
        case 2: return Cpu::OperandSize::long_word;
        default: throw UnsupportedSize{size};
    }
}

[[nodiscard]] std::uint32_t read_long(Bus &bus, std::uint32_t address) {
    std::uint32_t value = bus.read16(address);
    value <<= 16;
    value |= bus.read16(address + 2U);
    return value;
}

void write_long(Bus &bus, std::uint32_t address, std::uint32_t value) {
    bus.write16(address, static_cast<std::uint16_t>(value >> 16));
    bus.write16(address + 2U, static_cast<std::uint16_t>(value & 0xFFFFU));
}

[[nodiscard]] std::uint32_t address_step(Cpu::OperandSize size,
                                         std::uint8_t index) {
    switch (size) {
        case Cpu::OperandSize::byte: return index == 7 ? 2U : 1U;
        case Cpu::OperandSize::word: return 2U;
        case Cpu::OperandSize::long_word: return 4U;
    }
    return 0U;
}

[[nodiscard]] std::uint32_t read_memory(Bus &bus, std::uint32_t address,
                                        Cpu::OperandSize size) {
    switch (size) {
        case Cpu::OperandSize::byte: return bus.read8(address);
        case Cpu::OperandSize::word: return bus.read16(address);
        case Cpu::OperandSize::long_word: return read_long(bus, address);
    }
    return 0;
}

void write_memory(Bus &bus, std::uint32_t address, Cpu::OperandSize size,
                  std::uint32_t value) {
    switch (size) {
        case Cpu::OperandSize::byte:
            bus.write8(address, static_cast<std::uint8_t>(value));
            break;
        case Cpu::OperandSize::word:
            bus.write16(address, static_cast<std::uint16_t>(value));
            break;
        case Cpu::OperandSize::long_word:
            write_long(bus, address, value);
            break;
    }
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
    std::uint16_t opcode = bus.read16(pc_);
    pc_ += 2U;

    if (opcode == nop_opcode) {
        return;
    } else if (opcode == rts_opcode) {
        std::uint32_t return_address = read_long(bus, A_[7]);
        A_[7] += 4U;
        pc_ = return_address;
        return;
    } else if ((opcode & moveq_mask) == moveq_pattern) {
        execute_moveq(opcode);
        return;
    } else if ((opcode & branch_mask) == branch_pattern) {
        execute_branch(bus, opcode);
        return;
    } else if ((opcode & addq_subq_mask) == addq_subq_pattern &&
               (opcode & 0x00C0U) != 0x00C0U) {
        execute_addq_subq(bus, opcode);
        return;
    }

    throw UnsupportedInstruction{opcode};
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
        default: throw UnsupportedCondition{condition};
    }
}

std::uint32_t Cpu::execute_quick_arithmetic(std::uint32_t destination,
                                            std::uint32_t quick_data,
                                            OperandSize size, bool is_addq) {

    std::uint32_t size_mask = get_size_mask(size);
    std::uint32_t msb = (size_mask >> 1) + 1;
    destination &= size_mask;
    bool is_dest_neg = !!(destination & msb);
    std::uint32_t value =
        (is_addq ? destination + quick_data : destination - quick_data) &
        size_mask;
    bool is_val_neg = !!(value & msb);

    status_ &= static_cast<std::uint16_t>(~(
        zero_flag | negative_flag | extend_flag | overflow_flag | carry_flag));
    if (value == 0) {
        status_ |= zero_flag;
    }
    if (is_val_neg) {
        status_ |= negative_flag;
    }
    if ((is_addq && value < destination) || (!is_addq && destination < value)) {
        status_ |= carry_flag | extend_flag;
    }
    if ((is_addq && !is_dest_neg && is_val_neg) ||
        (!is_addq && is_dest_neg && !is_val_neg)) {
        status_ |= overflow_flag;
    }
    return value;
}

void Cpu::execute_addq_subq(Bus &bus, std::uint16_t opcode) {
    std::uint8_t mode = (opcode >> 3) & 0x07;
    if (mode > 6) {
        throw UnsupportedInstruction{opcode};
    }
    bool is_addq = !(opcode & 0x0100U);
    std::uint32_t quick_data = (opcode & 0x0E00U) >> 9;
    if (quick_data == 0) {
        quick_data = 8;
    }
    std::uint8_t index = opcode & 0x0007;
    if (mode == 0b001) {
        if ((opcode & 0x00C0) == 0) {
            throw UnsupportedInstruction{opcode};
        }
        if (is_addq) {
            A_[index] += quick_data;
        } else {
            A_[index] -= quick_data;
        }
        return;
    }
    auto size = decode_size((opcode & 0x00C0) >> 6);
    std::uint32_t size_mask = get_size_mask(size);
    if (mode == 0b010 || mode == 0b011 || mode == 0b100 || mode == 0b101 ||
        mode == 0b110) {
        if (mode == 0b100) {
            A_[index] -= address_step(size, index);
        }

        std::uint32_t address = A_[index];
        if (mode == 0b110) {
            std::uint16_t extension = bus.read16(pc_);
            pc_ += 2U;
            std::uint8_t displacement = extension & 0x00FFU;
            assert((extension & 0x0700U) == 0);
            std::uint8_t index = (extension & 0x7000U) >> 12;
            bool use_low_word = (extension & 0x0800) == 0;
            auto &R = (extension & 0x8000U) ? A_ : D_;
            const std::int32_t index_val =
                use_low_word ? static_cast<std::int32_t>(
                                   static_cast<std::int16_t>(R[index]))
                             : static_cast<std::int32_t>(R[index]);
            address = add_displacement(address, index_val);
            address = add_displacement(address,
                                       static_cast<std::int8_t>(displacement));
        }
        if (mode == 0b101) {
            std::uint16_t displacement = bus.read16(pc_);
            pc_ += 2;
            address = add_displacement(address,
                                       static_cast<std::int16_t>(displacement));
        }

        std::uint32_t destination = read_memory(bus, address, size);
        auto value =
            execute_quick_arithmetic(destination, quick_data, size, is_addq);
        write_memory(bus, address, size, value);
        if (mode == 0b011) {
            A_[index] += address_step(size, index);
        }
    } else {
        auto value =
            execute_quick_arithmetic(D_[index], quick_data, size, is_addq);
        D_[index] &= ~size_mask;
        D_[index] |= value;
    }
}

void Cpu::execute_branch(Bus &bus, std::uint16_t opcode) {
    std::uint8_t condition{static_cast<std::uint8_t>((opcode >> 8U) & 0x0FU)};

    std::int32_t displacement{static_cast<std::int8_t>(opcode & 0x00FFU)};
    std::uint32_t return_address{pc_};

    if (condition != 0 && condition != 1 && !condition_true(condition)) {
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
        return_address += 2U;
    }

    if (condition == 1) {
        // BSR (Branch to Subroutine)
        A_[7] -= 4U;
        write_long(bus, A_[7], return_address);
    }

    pc_ = add_displacement(pc_, displacement);
}

void Cpu::execute_moveq(std::uint16_t opcode) {
    std::size_t index{(opcode >> 9) & 0x0007U};
    std::int32_t immediate{static_cast<std::int8_t>(opcode & 0x00FFU)};
    D_[index] = static_cast<std::uint32_t>(immediate);
    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    if (immediate == 0) {
        status_ |= zero_flag;
    } else if (immediate < 0) {
        status_ |= negative_flag;
    }
}

} // namespace m68000
