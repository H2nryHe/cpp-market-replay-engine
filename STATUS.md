# Project Status

## Current phase
Phase 7 - Latency-Aware Execution

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 8/8 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for Phase 7 order IDs, arrival-event trace, lifecycle outcomes, fill sequence, fill prices, fill quantities, fees, and final order-book hash

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS
- Phase 3 - PASS
- Phase 4 - PASS
- Phase 5 - PASS
- Phase 6 - PASS
- Phase 7 - PASS

## Current work
- Phase 7 completed. Stopped before Phase 8.
- Added latency-aware execution integration over the Phase 4 `EventLoop` internal scheduler.
- Added `LatencyAwareExecution`, `LatencyExecutionConfig`, `OrderSubmissionRecord`, and `run_latency_aware_strategy`.
- Strategy `OrderIntent` objects are converted into execution-domain `Order` objects, transitioned to `Pending`, stored in a deterministic owner, and scheduled as `InternalEventType::OrderArrival`.
- Order arrival events carry only deterministic order ID metadata; no copied book snapshot or future execution result is stored in the scheduled event.
- Execution happens only when the `OrderArrival` internal event is processed and reads the current historical `OrderBook`.
- Added overflow-checked `checked_add_latency`.
- Added explicit decision timestamp accessor on `Order`; current decision and submit timestamps are equal.
- Added synthetic latency fixture where latency changes the execution price.
- Added Phase 7 tests for required cases A-R.
- Updated `docs/execution_model.md` for Phase 7 timing and scheduler semantics.

## Timing semantics
- Decision time: when a strategy emits `OrderIntent`.
- Submit time: when the simulator accepts the intent and creates an `Order`.
- Exchange arrival time: `submit_timestamp_ns + configured_latency.count`.
- Decision time and submit time are currently equal.
- Exchange arrival uses `TimestampNs` and `LatencyNs`; no wall-clock time is used.
- Negative user latency is rejected through `LatencyNs` construction helpers.
- Timestamp plus latency overflow throws `std::overflow_error`.

## Lifecycle changes
- Submitted orders transition `New -> Pending` immediately at strategy callback time.
- During latency, the order remains `Pending`, has `filled_quantity == 0`, and has no fills.
- At `OrderArrival`, market execution transitions from `Pending` to `Acknowledged`, then to the Phase 6 terminal status.
- Full fill: `New -> Pending -> Acknowledged -> Filled`.
- Partial fill with insufficient liquidity: `New -> Pending -> Acknowledged -> PartiallyFilled -> Canceled`.
- No executable liquidity at arrival: `New -> Pending -> Rejected`.
- Duplicate arrival dispatch for the same order is rejected explicitly because the order is no longer pending.

## Zero-latency semantics
- A latency of zero schedules `OrderArrival` at the current simulation timestamp.
- Zero latency does not execute inline inside a strategy callback.
- Historical market events at the same timestamp still precede internal events, preserving Phase 4 ordering.
- Same-timestamp test coverage verifies that a zero-latency order submitted after `t=100 seq1` executes only after later `t=100` market events have processed.

## Same-timestamp ordering
- Market events at timestamp `T` precede `OrderArrival` internal events at timestamp `T`.
- Multiple `OrderArrival` events with identical timestamps are processed in Phase 4 internal insertion order.
- Multiple pending market orders do not mutate historical depth; under zero market impact they may observe the same historical visible liquidity.

## End-of-feed behavior
- The event loop continues processing internal events after historical feed exhaustion.
- A pending order whose arrival time is after the final market event executes against the final historical `OrderBook` state.

## Zero-impact book policy
- Simulated execution reads historical visible liquidity but does not mutate the authoritative historical `OrderBook`.
- No execution-local shared liquidity model was added in Phase 7.
- Market impact is not modeled.

## Order lifecycle transition policy
- Allowed transitions:
  - `New -> Pending`
  - `Pending -> Acknowledged`
  - `Pending -> Rejected`
  - `Acknowledged -> PartiallyFilled`
  - `Acknowledged -> Filled`
  - `Acknowledged -> Canceled`
  - `PartiallyFilled -> Filled`
  - `PartiallyFilled -> Canceled`
- Terminal states `Filled`, `Canceled`, and `Rejected` have no outgoing transitions.
- Invalid transitions throw `std::invalid_argument`.

## Fill model
- Fill sequence IDs are simulator-local monotonic integers.
- Buy market orders fill asks from lowest to higher price.
- Sell market orders fill bids from highest to lower price.
- Fill timestamps equal the order exchange-arrival timestamp.
- Fill price is `PriceTicks`; no floating-point price is stored in canonical fill or order state.

## Fee representation / rounding
- Fee rate is a fixed-point integer in parts per million: `fee_rate_ppm`.
- Valid fee rates are `[0, 1,000,000]`.
- Canonical notional unit is `price_ticks * quantity`.
- Fees are calculated per fill as `round_half_up(notional_tick_quantity * fee_rate_ppm / 1,000,000)`.
- Fee calculations use integer arithmetic with checked intermediate range.

## Limit behavior
- `OrderType::Limit` remains structurally represented for future compatibility.
- Phase 7 still rejects limit execution explicitly as `New -> Pending -> Rejected`.
- No marketable limit execution, resting order, cancel handling, passive queue fill, or queue-ahead model is implemented.

## Event ordering policy
- Primary key: `timestamp_ns` ascending.
- At the same timestamp, historical market events are processed before internal scheduled events.
- Among historical market events, Phase 2 source order and `EventKey` ordering are preserved.
- Among internal events at the same timestamp, deterministic insertion order is preserved by `internal_sequence_id`.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- Phase 8 resting limit orders, cancels, cancellation races, passive fills, and queue-ahead modeling are not implemented.
- Phase 9 portfolio accounting, inventory, cash, PnL, and equity are not implemented.
- No market impact, benchmarking, Python bindings, multithreading, or performance optimization exists yet.

## Next phase
Phase 8 - Limit Orders, Cancel, Partial Fill & Passive Queue Approximation

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, execution_tests, and latency_execution_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 8/8 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ ./build/latency_execution_tests
Result: PASS - Phase 7 latency/execution tests passed.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 8/8 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ rg "queue_fraction|queue_ahead|CancelRequest|cancel request|passive_fill|passive fill|resting order|resting_order|Portfolio|class Portfolio|realized|unrealized|equity|Sharpe|drawdown|returns|PnL|pnl" include/replay src tests/unit docs CMakeLists.txt
Result: PASS - no Phase 8+ implementation introduced; matches are documented limitations or unrelated top-N wording.

$ rg "std::thread|std::mutex|std::atomic|std::async|random_device|system_clock|uuid" include/replay src tests/unit CMakeLists.txt
Result: PASS - no threading, randomness, wall-clock, or UUID behavior introduced.

$ rg "\bdouble\b|\bfloat\b" include/replay/order.hpp include/replay/fill.hpp include/replay/execution_simulator.hpp include/replay/latency_execution.hpp src/order.cpp src/fill.cpp src/execution_simulator.cpp src/latency_execution.cpp tests/unit/execution_simulator_test.cpp tests/unit/latency_execution_test.cpp
Result: PASS - no floating-point execution price/order/fill state introduced.

$ git diff --check
Result: PASS - no whitespace errors.

$ git status --short
Result: PASS - only Phase 7 files are modified/untracked; no staged changes.

$ git status --ignored --short
Result: PASS - `PROJECT_SPEC.md` and build directories are ignored.

$ git check-ignore -v PROJECT_SPEC.md
Result: PASS - `PROJECT_SPEC.md` is ignored by `.gitignore`.

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

## Phase 5 acceptance gate
- strategy interface cleanly separated: PASS
- QI demo works: PASS
- no direct order-book mutation: PASS
- no future-event access: PASS
- strategy behavior unit tested: PASS

## Phase 6 acceptance gate
- market orders walk depth correctly: PASS
- partial execution supported: PASS
- fees deterministic: PASS
- lifecycle transition table documented: PASS
- fill invariants tested: PASS

## Phase 7 acceptance gate
- no execution uses stale decision-time book by accident: PASS
- intervening events processed correctly: PASS
- latency configuration works: PASS
- deterministic tests pass: PASS

## Privacy / Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored: PASS
- no private/local-only file staged: PASS
- no staged changes: PASS
- no absolute user-machine paths introduced into public files: PASS
- fixtures are synthetic and publishable: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/execution_model.md`
- `include/replay/event_loop.hpp`
- `include/replay/execution_simulator.hpp`
- `include/replay/latency_execution.hpp`
- `include/replay/order.hpp`
- `src/execution_simulator.cpp`
- `src/latency_execution.cpp`
- `src/order.cpp`
- `STATUS.md`
- `tests/fixtures/latency_execution_book_updates.csv`
- `tests/unit/latency_execution_test.cpp`
