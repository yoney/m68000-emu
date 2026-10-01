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

constexpr std::uint16_t clr_mask = 0xFF00U;
constexpr std::uint16_t clr_pattern = 0x4200U;

constexpr std::uint16_t tst_mask = 0xFF00U;
constexpr std::uint16_t tst_pattern = 0x4A00U;

constexpr std::uint16_t cmp_mask = 0xF100U;
constexpr std::uint16_t cmp_pattern = 0xB000U;

constexpr std::uint16_t jmp_mask = 0xFFC0U;
constexpr std::uint16_t jmp_pattern = 0x4EC0U;

constexpr std::uint16_t jsr_mask = 0xFFC0U;
constexpr std::uint16_t jsr_pattern = 0x4E80U;

constexpr std::uint16_t lea_mask = 0xF1C0U;
constexpr std::uint16_t lea_pattern = 0x41C0U;

constexpr std::uint16_t pea_mask = 0xFFC0U;
constexpr std::uint16_t pea_pattern = 0x4840U;

constexpr std::uint16_t swap_mask = 0xFFF8U;
constexpr std::uint16_t swap_pattern = 0x4840U;

constexpr std::uint16_t ext_word_mask = 0xFFF8U;
constexpr std::uint16_t ext_word_pattern = 0x4880U;

constexpr std::uint16_t ext_long_mask = 0xFFF8U;
constexpr std::uint16_t ext_long_pattern = 0x48C0U;

constexpr std::uint16_t not_mask = 0xFF00U;
constexpr std::uint16_t not_pattern = 0x4600U;

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
    } else if ((opcode & clr_mask) == clr_pattern &&
               (opcode & 0x00C0U) != 0x00C0U) {
        execute_clr(bus, opcode);
        return;
    } else if ((opcode & tst_mask) == tst_pattern &&
               (opcode & 0x00C0U) != 0x00C0U) {
        execute_tst(bus, opcode);
        return;
    } else if ((opcode & cmp_mask) == cmp_pattern &&
               (opcode & 0x00C0U) != 0x00C0U) {
        execute_cmp(bus, opcode);
        return;
    } else if ((opcode & jmp_mask) == jmp_pattern) {
        execute_jmp(bus, opcode);
        return;
    } else if ((opcode & jsr_mask) == jsr_pattern) {
        execute_jsr(bus, opcode);
        return;
    } else if ((opcode & lea_mask) == lea_pattern) {
        execute_lea(bus, opcode);
        return;
    } else if ((opcode & pea_mask) == pea_pattern &&
               (opcode & 0x0038U) != 0x0000U) {
        execute_pea(bus, opcode);
        return;
    } else if ((opcode & swap_mask) == swap_pattern) {
        execute_swap(opcode);
        return;
    } else if ((opcode & ext_word_mask) == ext_word_pattern ||
               (opcode & ext_long_mask) == ext_long_pattern) {
        execute_ext(opcode);
        return;
    }
    if ((opcode & not_mask) == not_pattern && (opcode & 0x00C0U) != 0x00C0U) {
        execute_not(bus, opcode);
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
        mode == 0b110 || mode == 0b111) {
        if (mode == 0b111 && index > 1) {
            throw UnsupportedInstruction{opcode};
        }
        auto resolved = resolve_memory_address(bus, opcode, mode, index, size);
        std::uint32_t destination = read_memory(bus, resolved.address, size);
        auto value =
            execute_quick_arithmetic(destination, quick_data, size, is_addq);
        write_memory(bus, resolved.address, size, value);
        if (resolved.post_increment) {
            A_[index] += resolved.post_increment;
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
            pc_ += 2U;
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

// CLR.<size> <ea> - Clear an Operand
// Destination: Data alterable addressing modes (Dn and alterable memory modes).
// An direct mode is illegal.
// Condition codes: N = 0, Z = 1, V = 0, C = 0, X is unaffected.
void Cpu::execute_clr(Bus &bus, std::uint16_t opcode) {
    auto size = decode_size((opcode & 0x00C0U) >> 6);
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;
    if (mode == 0) {
        std::uint32_t size_mask = get_size_mask(size);
        D_[index] &= ~size_mask;
    } else if (mode == 0b111 && index > 1) {
        throw UnsupportedInstruction{opcode};
    } else {
        auto resolved = resolve_memory_address(bus, opcode, mode, index, size);
        // On physical MC68000 hardware, CLR performs an unneeded read cycle on
        // memory destinations before writing zero (read-modify-write bus
        // behavior).
        (void)read_memory(bus, resolved.address, size);
        write_memory(bus, resolved.address, size, 0U);
        if (resolved.post_increment) {
            A_[index] += resolved.post_increment;
        }
    }

    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    status_ |= zero_flag;
}

// TST.<size> <ea> - Test an Operand
// Destination: Data alterable addressing modes (Dn and alterable memory modes).
// An direct mode is illegal on MC68000.
// Condition codes: N = (value < 0), Z = (value == 0), V = 0, C = 0, X is
// unaffected.
void Cpu::execute_tst(Bus &bus, std::uint16_t opcode) {
    auto size = decode_size((opcode & 0x00C0U) >> 6);
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;
    std::uint32_t size_mask = get_size_mask(size);
    std::uint32_t value{0};
    if (mode == 0) {
        value = D_[index] & size_mask;
    } else if (mode == 0b111 && index > 1) {
        throw UnsupportedInstruction{opcode};
    } else {
        auto resolved = resolve_memory_address(bus, opcode, mode, index, size);
        value = read_memory(bus, resolved.address, size);
        if (resolved.post_increment) {
            A_[index] += resolved.post_increment;
        }
    }

    std::uint32_t msb = (size_mask >> 1) + 1;
    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    if (value == 0) {
        status_ |= zero_flag;
    } else if (value & msb) {
        status_ |= negative_flag;
    }
}

// CMP.<size> <ea>, Dn - Compare
// Operation: Dn - <ea>
// Condition codes: N = (result < 0), Z = (result == 0), V = overflow, C =
// borrow, X is unaffected.
void Cpu::execute_cmp(Bus &bus, std::uint16_t opcode) {
    auto size = decode_size((opcode & 0x00C0U) >> 6);
    std::uint32_t size_mask = get_size_mask(size);
    std::uint8_t d_index = (opcode & 0x0E00) >> 9;
    std::uint32_t lhs = D_[d_index] & size_mask;
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;
    std::uint32_t rhs{0};
    if (mode == 0) {
        rhs = D_[index] & size_mask;
    } else if (mode == 0b111 && index == 0b100) {
        switch (size) {
            case OperandSize::byte:
                rhs = bus.read16(pc_) & 0x00FFU;
                pc_ += 2U;
                break;

            case OperandSize::word:
                rhs = bus.read16(pc_);
                pc_ += 2U;
                break;

            case OperandSize::long_word:
                rhs = read_long(bus, pc_);
                pc_ += 4U;
                break;
        }
    } else if (mode == 0b001) {
        if (size == OperandSize::byte) {
            throw UnsupportedInstruction{opcode};
        }
        rhs = A_[index] & size_mask;
    } else {
        auto resolved = resolve_memory_address(bus, opcode, mode, index, size);
        rhs = read_memory(bus, resolved.address, size);
        if (resolved.post_increment) {
            A_[index] += resolved.post_increment;
        }
    }

    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    const std::uint32_t msb = (size_mask >> 1U) + 1U;
    std::uint32_t result = (lhs - rhs) & size_mask;
    if (result == 0U) {
        status_ |= zero_flag;
    }
    if ((result & msb) != 0U) {
        status_ |= negative_flag;
    }
    if (rhs > lhs) {
        status_ |= carry_flag;
    }
    if ((lhs ^ rhs) & (lhs ^ result) & msb) {
        status_ |= overflow_flag;
    }
}

void Cpu::execute_jmp(Bus &bus, std::uint16_t opcode) {
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;

    if (mode != 0b010 && mode != 0b101 && mode != 0b110 && mode != 0b111) {
        throw UnsupportedInstruction{opcode};
    }

    auto resolved = resolve_memory_address(bus, opcode, mode, index,
                                           OperandSize::long_word);
    pc_ = resolved.address;
}

void Cpu::execute_jsr(Bus &bus, std::uint16_t opcode) {
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;

    if (mode != 0b010 && mode != 0b101 && mode != 0b110 && mode != 0b111) {
        throw UnsupportedInstruction{opcode};
    }

    auto resolved = resolve_memory_address(bus, opcode, mode, index,
                                           OperandSize::long_word);

    A_[7] -= 4U;
    write_long(bus, A_[7], pc_);
    pc_ = resolved.address;
}

void Cpu::execute_lea(Bus &bus, std::uint16_t opcode) {
    std::uint8_t destination_index = (opcode >> 9) & 0x0007U;
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;

    if (mode != 0b010 && mode != 0b101 && mode != 0b110 && mode != 0b111) {
        throw UnsupportedInstruction{opcode};
    }

    auto resolved = resolve_memory_address(bus, opcode, mode, index,
                                           OperandSize::long_word);

    A_[destination_index] = resolved.address;
}

void Cpu::execute_pea(Bus &bus, std::uint16_t opcode) {
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;

    if (mode != 0b010 && mode != 0b101 && mode != 0b110 && mode != 0b111) {
        throw UnsupportedInstruction{opcode};
    }

    auto resolved = resolve_memory_address(bus, opcode, mode, index,
                                           OperandSize::long_word);
    A_[7] -= 4U;
    write_long(bus, A_[7], resolved.address);
}

void Cpu::execute_swap(std::uint16_t opcode) {
    std::size_t index = opcode & 0x0007U;
    D_[index] = (D_[index] << 16U) | (D_[index] >> 16U);
    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    if (D_[index] == 0) {
        status_ |= zero_flag;
    } else if ((D_[index] & 0x80000000U) != 0) {
        status_ |= negative_flag;
    }
}

void Cpu::execute_ext(std::uint16_t opcode) {
    const std::size_t index = opcode & 0x0007U;
    const auto opmode = static_cast<std::uint8_t>((opcode >> 6U) & 0x0007U);

    status_ &= static_cast<std::uint16_t>(~nzvc_flags);

    if (opmode == 0b010U) { // EXT.W: byte -> word (bits 31-16 preserved)
        const auto byte_val = static_cast<std::int8_t>(D_[index] & 0x00FFU);
        const auto word_val = static_cast<std::int16_t>(byte_val);
        D_[index] =
            (D_[index] & 0xFFFF0000U) | static_cast<std::uint16_t>(word_val);

        if (word_val == 0) {
            status_ |= zero_flag;
        } else if (word_val < 0) {
            status_ |= negative_flag;
        }
    } else if (opmode == 0b011U) { // EXT.L: word -> long
        const auto word_val = static_cast<std::int16_t>(D_[index] & 0xFFFFU);
        const auto long_val = static_cast<std::int32_t>(word_val);
        D_[index] = static_cast<std::uint32_t>(long_val);

        if (long_val == 0) {
            status_ |= zero_flag;
        } else if (long_val < 0) {
            status_ |= negative_flag;
        }
    } else {
        throw UnsupportedInstruction{opcode};
    }
}

void Cpu::execute_not(Bus &bus, std::uint16_t opcode) {
    std::uint8_t index = opcode & 0x0007U;
    std::uint8_t mode = (opcode >> 3) & 0x0007U;
    OperandSize size = decode_size((opcode >> 6) & 0x0003U);
    std::uint32_t size_mask = get_size_mask(size);
    std::uint32_t result{0};

    if (mode == 0b001 || (mode == 0b111 && index > 1)) {
        throw UnsupportedInstruction{opcode};
    }

    if (mode == 0b000) {
        result = (~D_[index]) & size_mask;
        D_[index] = (D_[index] & ~size_mask) | result;
    } else {
        auto resolved = resolve_memory_address(bus, opcode, mode, index, size);
        auto value = read_memory(bus, resolved.address, size);
        result = (~value) & size_mask;
        write_memory(bus, resolved.address, size, result);
        if (resolved.post_increment) {
            A_[index] += resolved.post_increment;
        }
    }

    std::uint32_t msb = (size_mask >> 1) + 1;
    status_ &= static_cast<std::uint16_t>(~nzvc_flags);
    if (result == 0) {
        status_ |= zero_flag;
    } else if (result & msb) {
        status_ |= negative_flag;
    }
}

Cpu::ResolvedAddress Cpu::resolve_memory_address(Bus &bus, std::uint16_t opcode,
                                                 std::uint8_t mode,
                                                 std::uint8_t address_register,
                                                 OperandSize size) {
    ResolvedAddress resolved{};
    switch (mode) {
        case 0b010: resolved.address = A_[address_register]; break;
        case 0b011: {
            resolved.address = A_[address_register];
            resolved.post_increment = address_step(size, address_register);
            break;
        }
        case 0b100: {
            A_[address_register] -= address_step(size, address_register);
            resolved.address = A_[address_register];
            break;
        }
        case 0b101: {
            std::uint16_t displacement = bus.read16(pc_);
            pc_ += 2U;
            resolved.address = add_displacement(
                A_[address_register], static_cast<std::int16_t>(displacement));
            break;
        }
        case 0b110: {
            std::uint16_t extension = bus.read16(pc_);
            pc_ += 2U;
            std::uint8_t displacement = extension & 0x00FFU;
            std::uint8_t index_reg = (extension & 0x7000U) >> 12;
            bool use_low_word = (extension & 0x0800) == 0;
            auto &R = (extension & 0x8000U) ? A_ : D_;
            const std::int32_t index_val =
                use_low_word ? static_cast<std::int32_t>(
                                   static_cast<std::int16_t>(R[index_reg]))
                             : static_cast<std::int32_t>(R[index_reg]);
            resolved.address =
                add_displacement(A_[address_register], index_val);
            resolved.address = add_displacement(
                resolved.address, static_cast<std::int8_t>(displacement));
            break;
        }
        case 0b111: {
            if (address_register == 0) {
                resolved.address = static_cast<std::uint32_t>(
                    static_cast<std::int16_t>(bus.read16(pc_)));
                pc_ += 2U;
            } else if (address_register == 1) {
                resolved.address = read_long(bus, pc_);
                pc_ += 4U;
            } else if (address_register == 2) {
                std::uint16_t displacement = bus.read16(pc_);
                resolved.address = add_displacement(
                    pc_, static_cast<std::int16_t>(displacement));
                pc_ += 2U;
            } else if (address_register == 3) {
                std::uint16_t extension = bus.read16(pc_);
                std::uint8_t displacement = extension & 0x00FFU;
                std::uint8_t index_reg = (extension & 0x7000U) >> 12;
                bool use_low_word = (extension & 0x0800U) == 0;
                auto &R = (extension & 0x8000U) ? A_ : D_;
                const std::int32_t index_val =
                    use_low_word ? static_cast<std::int32_t>(
                                       static_cast<std::int16_t>(R[index_reg]))
                                 : static_cast<std::int32_t>(R[index_reg]);
                resolved.address = add_displacement(pc_, index_val);
                resolved.address = add_displacement(
                    resolved.address, static_cast<std::int8_t>(displacement));
                pc_ += 2U;
            } else {
                throw UnsupportedInstruction{opcode};
            }
            break;
        }
        default: throw UnsupportedInstruction{opcode};
    }

    return resolved;
}

} // namespace m68000
