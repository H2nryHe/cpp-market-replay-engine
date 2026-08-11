# Project Status

## Current phase
Phase 5 - Strategy Interface + Queue Imbalance Demo

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 6/6 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for Phase 5 callback trace, intent sequence, intent contents, and final order-book hash

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS
- Phase 3 - PASS
- Phase 4 - PASS
- Phase 5 - PASS

## Current work
- Phase 5 completed. Stopped before Phase 6.
- Implemented a minimal `Strategy` interface with `on_book`, `on_trade`, and `on_timer` callbacks.
- Implemented `OrderIntent`, `IntentSink`, `VectorIntentSink`, and `run_strategy` as a thin strategy layer over the Phase 4 event loop.
- Implemented `QueueImbalanceStrategy` as a deterministic demo strategy using configurable top-N depth and thresholds.
- Added strategy tests for callback order, post-update book visibility, read-only strategy signatures, no-lookahead, QI buy/sell/no-action behavior, top-N depth, zero/one-sided/locked/crossed policies, intent content, same-timestamp causality, timer callback, determinism, and strategy state-separation.
- Documented Phase 5 behavior in `docs/strategy.md`.

## Strategy callback semantics
- `BookUpdateEvent`: event loop processes the event, applies the update to `OrderBook`, then invokes `Strategy::on_book`.
- `TradeEvent`: event loop preserves source order, does not mutate the book, then invokes `Strategy::on_trade`.
- `InternalEventType::Timer`: `Strategy::on_timer` is invoked for timer events only.
- Callbacks occur once per relevant event in exact event-loop order; same-timestamp market events are not batched.
- Strategy receives `const OrderBook&` and const market event data.

## OrderIntent design
- `OrderIntent` is a strategy-level decision record, not a Phase 6 order.
- Fields: `side`, desired `quantity`, `order_type`, optional `limit_price_ticks`, and `decision_timestamp_ns`.
- It has no order ID, exchange-arrival time, acknowledgement, status machine, fill quantity, cancel lifecycle, or execution state.

## Queue Imbalance formula
- `QI = (bid_volume - ask_volume) / (bid_volume + ask_volume)`.
- Bid/ask volume is summed from configurable top-N visible depth.
- If `QI > buy_threshold`, emit one Buy intent.
- If `QI < sell_threshold`, emit one Sell intent.
- Otherwise emit no intent.

## QI numeric representation
- Prices remain `PriceTicks`; quantities remain `Quantity`.
- QI uses `double` only for the derived dimensionless ratio after integer volume summation.
- QI floating-point values are not used for price representation, book keys, or canonical market state.

## QI edge policies
- Zero selected volume: emit no intent.
- One-sided book: emit no intent.
- Locked or crossed book: emit no intent.
- Duplicate-signal behavior: evaluate on every `on_book` callback and emit at most one intent per callback when thresholds are crossed.

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
- Phase 6 order lifecycle and market execution are not implemented.
- `OrderIntent` generation alone does not affect replay state because execution does not exist yet.
- No real orders, order IDs, acknowledgements, fills, execution simulator, latency execution, passive queue fills, portfolio, PnL, transaction fees, benchmarking, Python bindings, multithreading, or performance optimization exists yet.

## Next phase
Phase 6 - Order Lifecycle + Market Execution

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, and strategy_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 6/6 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 6/6 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build/strategy_tests
Result: PASS - Phase 5 strategy tests passed.

$ rg "#include .*execution_simulator|#include .*portfolio|#include .*fill" include src strategies tests/unit/strategy_test.cpp CMakeLists.txt
Result: PASS - strategy code does not include future execution, fill, or portfolio headers.

$ rg "class Order\b|order_id|filled_quantity|OrderStatus|Acknowledged|PartiallyFilled|execution_state|portfolio|PnL|pnl|fee" include/replay/strategy.hpp src/strategy.cpp strategies tests/unit/strategy_test.cpp
Result: PASS - no Phase 6+ order lifecycle, execution, portfolio, PnL, or fee behavior in strategy implementation; only `market_feed` include text and a test message matched broader substrings.

$ rg "std::thread|std::mutex|std::atomic|std::async|random_device|system_clock" include src strategies tests apps CMakeLists.txt
Result: PASS - no threading, atomics, randomness, or wall-clock ordering introduced.

$ git status --short
Result: PASS - only Phase 5 files are modified/untracked; no staged changes.

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

## Phase 5 acceptance gate
- strategy interface cleanly separated: PASS
- QI demo works: PASS
- no direct order-book mutation: PASS
- no future-event access: PASS
- strategy behavior unit tested: PASS

## Privacy / Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored: PASS
- no private/local-only file staged: PASS
- no staged changes: PASS
- no absolute user-machine paths introduced into public files: PASS
- fixtures are synthetic and publishable: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/strategy.md`
- `include/replay/strategy.hpp`
- `STATUS.md`
- `src/strategy.cpp`
- `strategies/queue_imbalance_strategy.hpp`
- `strategies/queue_imbalance_strategy.cpp`
- `tests/unit/strategy_test.cpp`
