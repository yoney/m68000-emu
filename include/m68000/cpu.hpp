#pragma once

#include <array>
#include <cstdint>

namespace m68000 {

class Bus;

class Cpu {
public:
    Cpu() = default;

    void reset(Bus &bus);

    [[nodiscard]] std::uint32_t A(std::size_t index) const noexcept {
        return A_[index];
    }
    [[nodiscard]] std::uint32_t D(std::size_t index) const noexcept {
        return D_[index];
    }
    [[nodiscard]] std::uint32_t pc() const noexcept { return pc_; }
    [[nodiscard]] std::uint16_t status() const noexcept { return status_; }

private:
    std::array<std::uint32_t, 8> D_{}; // D0-D7
    std::array<std::uint32_t, 8> A_{}; // A0-A7
    std::uint32_t pc_{};
    std::uint16_t status_{};
};

} // namespace m68000
