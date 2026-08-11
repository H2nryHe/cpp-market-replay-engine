# Project Status

## Current phase
Phase 3 - L2 Order Book Reconstruction

## Phase status
PASS

## Last verified commit
Not available - workspace is not currently a Git repository.

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 4/4 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for 100 repeated golden book replays and stable canonical state hash

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS
- Phase 3 - PASS

## Current work
- Phase 3 completed. Stopped before Phase 4.
- Implemented deterministic visible L2 `OrderBook` consuming Phase 2 `BookUpdateEvent`.
- Implemented `apply`, best bid/ask, bid/ask depth lookup, top-N snapshots, level counts, empty/one-sided handling, spread, exact midpoint, locked/crossed flags, canonical state, and stable state hash.
- Added Phase 3 golden order-book fixture under `tests/golden/`.
- Added order-book unit tests for insert, price priority, replace semantics, delete, absent delete, empty/one-sided behavior, spread, half-tick mid, top-N, locked/crossed states, extreme values, golden replay, determinism, and invariants.
- Documented Phase 3 behavior in `docs/order_book.md`.

## Book data structure
- Bids: `std::map<PriceTicks, Quantity, std::greater<PriceTicks>>`, best to worst.
- Asks: `std::map<PriceTicks, Quantity, std::less<PriceTicks>>`, best to worst.
- `BookUpdateEvent.quantity` is treated as resulting visible quantity, not an additive delta.

## Midpoint representation
- `mid_price_x2_ticks()` returns `best_bid_ticks + best_ask_ticks`.
- This exactly represents half-tick midpoints without binary floating point or integer truncation.
- Overflow throws `std::overflow_error`.

## Crossed-book policy
- Source-observed locked/crossed states are applied faithfully.
- The book does not silently reorder, delete, or alter levels to uncross the market.
- `is_locked()`, `is_crossed()`, and `is_valid_two_sided_market()` expose state.

## Canonical hash method
- `canonical_state()` encodes bids best-to-worst, then asks best-to-worst, as `B,<price_ticks>,<quantity>\n` and `A,<price_ticks>,<quantity>\n`.
- `state_hash()` applies FNV-1a 64-bit with offset basis `14695981039346656037` and prime `1099511628211`.
- The hash is a deterministic regression checksum, not a cryptographic claim.

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
- Phase 4 replay/event scheduling is not implemented.
- No simulation clock, strategy, orders, fills, execution simulator, latency model, portfolio, benchmarking, Python bindings, multithreading, or performance optimization exists yet.

## Next phase
Phase 4 - Simulation Clock & Deterministic Event Loop

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, and order_book_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 4/4 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 4/4 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build/order_book_tests
Result: PASS - Phase 3 order-book tests passed.

$ rg "std::stod|stod\(|\bfloat\b|\bdouble\b" include src tests apps CMakeLists.txt
Result: PASS - no implementation/test floating-point price parsing or order-book price-key use found.

$ rg "SimulationClock|simulation_clock|ReplayEngine|replay_engine|Strategy|ExecutionSimulator|execution_simulator|Portfolio|portfolio|pybind|thread|benchmark" include src tests apps CMakeLists.txt docs
Result: PASS - later-phase terms appear only in documentation stating those features are not implemented; project name in CMake also matches `replay`.
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

## Phase 3 acceptance gate
- all book operations correct: PASS
- edge cases tested: PASS
- golden replay passes: PASS
- deterministic state hash stable: PASS
- no negative quantities: PASS
- sanitizer tests pass: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/order_book.md`
- `include/replay/order_book.hpp`
- `STATUS.md`
- `src/order_book.cpp`
- `tests/golden/order_book_updates.csv`
- `tests/unit/order_book_test.cpp`
