#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <stdexcept>

namespace m68000 {

class Bus;

constexpr std::uint16_t carry_flag = 1U << 0;
constexpr std::uint16_t overflow_flag = 1U << 1;
constexpr std::uint16_t zero_flag = 1U << 2;
constexpr std::uint16_t negative_flag = 1U << 3;
constexpr std::uint16_t extend_flag = 1U << 4;

constexpr std::uint16_t nzvc_flags =
    negative_flag | zero_flag | overflow_flag | carry_flag;

class Cpu {
public:
    Cpu() = default;

    void reset(Bus &bus);
    void step(Bus &bus);

    [[nodiscard]] std::uint32_t A(std::size_t index) const noexcept {
        return A_[index];
    }
    [[nodiscard]] std::uint32_t D(std::size_t index) const noexcept {
        return D_[index];
    }
    [[nodiscard]] std::uint32_t pc() const noexcept { return pc_; }
    [[nodiscard]] std::uint16_t status() const noexcept { return status_; }

    enum class OperandSize : std::uint8_t {
        byte,
        word,
        long_word,
    };

private:
    [[nodiscard]] bool condition_true(std::uint8_t condition) const;
    [[nodiscard]] std::uint32_t
    execute_quick_arithmetic(std::uint32_t destination,
                             std::uint32_t quick_data, OperandSize size,
                             bool is_addq);
    void execute_addq_subq(Bus &bus, std::uint16_t opcode);
    void execute_branch(Bus &bus, std::uint16_t opcode);
    void execute_moveq(std::uint16_t opcode);

    std::array<std::uint32_t, 8> D_{}; // D0-D7
    std::array<std::uint32_t, 8> A_{}; // A0-A7
    std::uint32_t pc_{};
    std::uint16_t status_{};
};

struct UnsupportedInstruction : public std::runtime_error {
    UnsupportedInstruction(std::uint16_t opcode)
        : std::runtime_error{std::format("0x{:04X}", opcode)} {}
};

struct UnsupportedCondition : public std::runtime_error {
    UnsupportedCondition(std::uint8_t cond)
        : std::runtime_error{std::format("0x{:02X}", cond)} {}
};

struct UnsupportedSize : public std::runtime_error {
    UnsupportedSize(std::uint8_t size)
        : std::runtime_error{std::format("0x{:02X}", size)} {}
};

} // namespace m68000
