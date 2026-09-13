#pragma once

#include <cstdint>

namespace m68000 {

class Bus {
public:
    virtual ~Bus() = default;

    virtual std::uint8_t read8(std::uint32_t address) = 0;
    virtual std::uint16_t read16(std::uint32_t address) = 0;

    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t address, std::uint16_t value) = 0;
};

} // namespace m68000
