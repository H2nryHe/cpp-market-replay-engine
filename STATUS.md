# Project Status

## Current phase
Phase 4 - Simulation Clock & Deterministic Event Loop

## Phase status
PASS

## Last verified commit
6c2e761

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 5/5 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for Phase 4 deterministic trace, final simulation time, and final order-book hash

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS
- Phase 3 - PASS
- Phase 4 - PASS

## Current work
- Phase 4 completed. Stopped before Phase 5.
- Implemented `SimulationClock` with monotonic `TimestampNs` advancement and explicit backwards-advance rejection.
- Implemented deterministic single-threaded `InternalEventScheduler` with monotonic internal insertion sequence IDs.
- Implemented `EventLoop` that merges Phase 2 historical market events with internal scheduled events using an explicit total ordering.
- Added dispatch hooks for market/internal events and optional Phase 3 `OrderBook` application for `BookUpdateEvent`.
- Added deterministic trace entries, canonical trace encoding, and FNV-1a 64-bit trace hashing.
- Added Phase 4 tests for clock monotonicity, historical ordering, timestamp ties, market/internal interleaving, same-timestamp precedence, internal insertion order, past/current scheduling, empty/internal-only replay, book integration, trade dispatch, determinism, no-lookahead, end-of-stream with pending internals, single-event cases, and timestamp-limit edge behavior.
- Documented Phase 4 behavior in `docs/event_loop.md`.

## Event ordering policy
- Primary key: `timestamp_ns` ascending.
- At the same timestamp, historical market events are processed before internal scheduled events.
- Among historical market events, Phase 2 source order and `EventKey` ordering are preserved; the event loop does not silently sort or repair market data.
- Among internal events at the same timestamp, deterministic insertion order is preserved by `internal_sequence_id`.

## Same-timestamp precedence
- Market event at timestamp `T` precedes any internal event scheduled for timestamp `T`.
- Internal events scheduled at the current simulation time are accepted and processed according to the same rule.

## Internal scheduler ordering rule
- Scheduling in the future is accepted.
- Scheduling at the current simulation time is accepted.
- Scheduling in the past is rejected with an exception.
- Duplicate payloads are allowed.
- Internal events with equal timestamps are ordered by deterministic monotonic `internal_sequence_id`.

## End-of-stream policy
- The event loop terminates only after both the historical market stream is exhausted and the internal scheduler is empty.
- Pending internal events after the last historical market event are processed.

## Deterministic trace method
- Trace lines use `M,<timestamp_ns>,<market_event_type>,<market_sequence_id>` for market events.
- Trace lines use `I,<timestamp_ns>,<internal_event_type>,<internal_sequence_id>,<label>` for internal events.
- `EventLoopResult::trace_hash()` applies FNV-1a 64-bit to the canonical trace as a deterministic regression checksum.

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
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- Phase 5 strategy interface is not implemented.
- Internal event categories are generic scheduling payloads only; no order, cancel, fill, execution, or latency behavior is implemented.
- No strategy, order lifecycle, fills, execution simulator, latency model, portfolio, benchmarking, Python bindings, multithreading, or performance optimization exists yet.

## Next phase
Phase 5 - Strategy Interface + Queue Imbalance Demo

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, and event_loop_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 5/5 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 5/5 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build/event_loop_tests
Result: PASS - Phase 4 event-loop tests passed.

$ rg "std::chrono::system_clock|random_device|std::thread|std::mutex|std::atomic|async\(|unordered_" include src tests apps CMakeLists.txt
Result: PASS - no wall-clock, randomness, thread, mutex, atomic, async, or unordered-container ordering source found.

$ git status --short
Result: PASS - only Phase 4 files are modified/untracked; no staged changes.

$ git status --ignored --short
Result: PASS - `PROJECT_SPEC.md` and build directories are ignored.

$ git check-ignore -v PROJECT_SPEC.md cpp-market-replay-engine_PROJECT_SPEC.md
Result: PASS - `PROJECT_SPEC.md` is ignored by `.gitignore`; `cpp-market-replay-engine_PROJECT_SPEC.md` is not reported in Git status.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!cpp-market-replay-engine_PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!.git/**'
Result: PASS - no absolute user-machine paths found in public files.
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

## Phase 4 acceptance gate
- event loop is single-threaded: PASS
- tie-breaking documented: PASS
- internal events interleave correctly with market events: PASS
- deterministic trace test passes: PASS
- no later phase logic embedded prematurely: PASS

## Privacy / Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored: PASS
- no private/local-only file staged: PASS
- no staged changes: PASS
- no absolute user-machine paths introduced into public files: PASS
- fixtures are synthetic and publishable: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/event_loop.md`
- `include/replay/event_loop.hpp`
- `include/replay/simulation_clock.hpp`
- `STATUS.md`
- `src/event_loop.cpp`
- `src/simulation_clock.cpp`
- `tests/unit/event_loop_test.cpp`
