# Contributing to hevc.js

Thanks for your interest in contributing! Here's how to get started.

## Development setup

### Prerequisites

- Node.js >= 18
- [pnpm](https://pnpm.io/)
- CMake >= 3.16
- C++17 compiler (clang or gcc)
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (for WASM builds)

### Build

```bash
# Clone and install
git clone https://github.com/privaloops/hevc.js.git
cd hevc.js
pnpm install

# Native build (debug + tests)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure

# WASM build
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm

# JS packages
pnpm -r build
```

### Running tests

```bash
# C++ unit + oracle tests (128 tests)
pnpm test:native

# E2E browser tests (requires built WASM + demo bundles)
pnpm build:demo
pnpm test:e2e
```

## Pull request process

1. Fork the repo and create a branch (`feature/xxx` or `fix/xxx`)
2. Make your changes with clear, atomic commits (conventional commits: `feat:`, `fix:`, `docs:`, etc.)
3. Ensure all tests pass
4. Open a PR against `main`

## Code style

- **C++**: C++17, compiled with `-Wall -Wextra -Wpedantic -Werror`
- **TypeScript**: strict mode, ES2020 target, ESM modules
- Variable/function names follow the ITU-T H.265 spec for decoder code

## Reporting bugs

Open an issue at https://github.com/privaloops/hevc.js/issues with:
- Steps to reproduce
- Expected vs actual behavior
- Browser/OS/architecture
- Sample bitstream if applicable
