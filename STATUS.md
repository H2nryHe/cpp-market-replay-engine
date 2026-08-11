# Project Status

## Current phase
Phase 1 - Core Domain Types

## Phase status
PASS

## Last verified commit
Not available - workspace is not currently a Git repository.

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 2/2 passed
- CLI smoke: PASS
- Golden replay: Not started
- Determinism: PASS for Phase 1 EventKey ordering

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS

## Current work
- Phase 1 completed. Stopped before Phase 2.
- Implemented strongly typed domain primitives: `TimestampNs`, `PriceTicks`, `Quantity`, `Side`, `OrderType`, `OrderStatus`, `EventKey`, and `LatencyNs`.
- Implemented deterministic decimal-string price/tick conversion with explicit no-rounding behavior.
- Implemented validation helpers and parser functions that reject invalid values explicitly.
- Documented Phase 1 units, price conversion rules, enum aliases, and EventKey ordering in `docs/domain_types.md`.

## Known limitations
- The workspace has no Git metadata, so commit tracking is unavailable.
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- Phase 2 market feed/parser is not implemented.
- No order book, replay loop, strategy, execution simulator, or portfolio exists yet.

## Next phase
Phase 2 - Market Feed & Normalized Parser

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, and domain_types_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 2/2 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 2/2 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ rg "\bdouble\b|std::map|std::unordered_map" include src tests apps docs CMakeLists.txt
Result: PASS - no matches; no order-book price-key containers exist in Phase 1.
```

## Phase 0 acceptance gate
- clean configure: PASS
- clean build: PASS
- CTest passes: PASS
- CLI `--help` works: PASS
- sanitizer build works where supported: PASS
- `STATUS.md` records exact commands/results: PASS

## Phase 1 acceptance gate
- no market price key uses `double`: PASS
- event ordering has deterministic tests: PASS
- conversion rules documented: PASS
- all unit tests pass: PASS
- sanitizer tests pass: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/domain_types.md`
- `include/replay/types.hpp`
- `STATUS.md`
- `src/types.cpp`
- `tests/unit/domain_types_test.cpp`
