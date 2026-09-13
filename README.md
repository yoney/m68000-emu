# m68000-emu

A from-scratch emulator for the original Motorola MC68000 CPU.

## Status

## Building and Testing

### Prerequisites

- CMake 3.20 or newer
- C++20 compliant compiler (GCC or Clang on Linux)

### Build Commands

```bash
cmake -S . -B build
cmake --build build
```

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```
