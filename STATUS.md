# Project Status

## Current phase
Phase 2 - Market Feed & Normalized Parser

## Phase status
PASS

## Last verified commit
Not available - workspace is not currently a Git repository.

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 3/3 passed
- CLI smoke: PASS
- Golden replay: Not started
- Determinism: PASS for Phase 2 source-order preservation and repeatable canonical event sequences

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS

## Current work
- Phase 2 completed. Stopped before Phase 3.
- Implemented normalized `BookUpdateEvent`, `TradeEvent`, `MarketEvent`, `NormalizedMarketFeed`, and actionable `ParseError`.
- Implemented simple normalized CSV parsers/loaders for L2 book updates and trades.
- Reused Phase 1 `price_to_ticks`, `parse_price_ticks`, `parse_quantity`, `parse_side`, and validation helpers.
- Added valid book/trade fixtures under `tests/fixtures/`.
- Added parser tests for valid fixtures, decimal price integrity, timestamp ties, duplicate/out-of-order keys, malformed input, empty input, repeatability, tick-price input, and parse-error context.
- Documented the Phase 2 data contract in `docs/data_contract.md`.

## Ordering policy
- Source row order is preserved; parser output is never silently sorted.
- Event keys must be strictly increasing by `(timestamp_ns, sequence_id)`.
- Increasing timestamps are accepted.
- Equal timestamps are accepted only when `sequence_id` increases.
- Duplicate event keys are rejected.
- Equal-timestamp decreasing sequence IDs and out-of-order timestamps are rejected.
- The same `sequence_id` may appear at different timestamps because the unique ordering key is the full `EventKey`.

## Known limitations
- The workspace has no Git metadata, so commit tracking is unavailable.
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- Phase 3 order-book reconstruction is not implemented.
- No replay loop, strategy, execution simulator, portfolio, benchmarking, or Python bindings exist yet.

## Next phase
Phase 3 - L2 Order Book Reconstruction

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, and market_feed_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 3/3 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 3/3 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build/market_feed_tests
Result: PASS - Phase 2 parser tests passed.

$ rg "std::stod|stod\(|\bfloat\b|\bdouble\b" include src tests apps CMakeLists.txt
Result: PASS - no implementation/test floating-point price parsing found.

$ rg "OrderBook|order_book|ReplayEngine|replay_engine|Strategy|ExecutionSimulator|execution_simulator|Portfolio|portfolio|pybind" include src tests apps CMakeLists.txt
Result: PASS - no later-phase components found; only the project name in CMake matched `replay`.
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

## Phase 2 acceptance gate
- parser handles valid fixture: PASS
- parser rejects malformed fixture: PASS
- deterministic ordering confirmed: PASS
- errors are actionable: PASS
- no premature optimization: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/data_contract.md`
- `docs/domain_types.md`
- `include/replay/market_feed.hpp`
- `include/replay/types.hpp`
- `STATUS.md`
- `src/market_feed.cpp`
- `src/types.cpp`
- `tests/fixtures/book_updates_valid.csv`
- `tests/fixtures/trades_valid.csv`
- `tests/unit/domain_types_test.cpp`
- `tests/unit/market_feed_test.cpp`
