# cpp-market-replay-engine

Deterministic C++20 market-data replay engine scaffold.

This repository is being built phase by phase from `PROJECT_SPEC.md`. Phase 0 provides a reproducible CMake project, library target, CLI target, CTest smoke test, warning flags, and optional sanitizer builds. Core market-domain primitives begin in Phase 1 and are intentionally not implemented in this bootstrap phase.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

```bash
./build/replay_cli --help
```

## Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## Current Scope

See `STATUS.md` for the current phase, verification commands, and known limitations.
