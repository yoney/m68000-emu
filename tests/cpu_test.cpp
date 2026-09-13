#include <gtest/gtest.h>

#include "m68000/cpu.hpp"

TEST(CpuTest, CanBeConstructed)
{
    [[maybe_unused]] m68000::Cpu cpu;
    SUCCEED();
}
