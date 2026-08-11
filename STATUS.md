# Project Status

## Current phase
Phase 6 - Order Lifecycle + Market Execution

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 7/7 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for Phase 6 order IDs, lifecycle outcomes, fill sequence, fill prices, fill quantities, fee values, and unchanged historical order-book hash

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

## Current work
- Phase 6 completed. Stopped before Phase 7.
- Implemented execution-domain `Order` with deterministic `OrderId`, submit timestamp, immediate exchange-arrival timestamp, side, type, original quantity, filled quantity, computed remaining quantity, optional limit price, current status, and status history.
- Implemented validated lifecycle transitions through `Order::transition_to`; invalid transitions throw explicitly.
- Implemented `Fill` records with order ID, side, price ticks, quantity, fill timestamp, deterministic fill sequence ID, and integer fee amount.
- Implemented `OrderFactory` and `ExecutionSimulator` with zero-latency market-order execution against read-only visible L2 depth.
- Implemented deterministic fixed-point `FeeModel` using `fee_rate_ppm`.
- Added Phase 6 execution tests covering required cases A-S, including one-level fills, multi-level sweeps, insufficient liquidity, empty/one-sided books, historical book immutability, fill conservation, no-overfill, exact fees, per-fill multi-fill fees, limit rejection, intent conversion, and 100-run determinism.
- Documented Phase 6 behavior in `docs/execution_model.md`.

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
- Invalid transitions such as `Filled -> Pending`, `Canceled -> Filled`, and `Rejected -> Filled` throw `std::invalid_argument`.
- Full immediate market fill: `New -> Pending -> Acknowledged -> Filled`.
- Partial immediate market fill with insufficient liquidity: `New -> Pending -> Acknowledged -> PartiallyFilled -> Canceled`.
- Zero executable liquidity: `New -> Pending -> Rejected`.

## Insufficient-liquidity policy
- Market orders sweep currently visible opposite-side L2 depth in price priority.
- If visible liquidity is insufficient, fills are kept, `remaining_quantity` is preserved, and the unfilled market-order remainder is canceled.
- A market-order remainder does not become a resting order in Phase 6.

## Fill model
- Fill sequence IDs are simulator-local monotonic integers.
- Buy market orders fill asks from lowest to higher price.
- Sell market orders fill bids from highest to lower price.
- Fill timestamps equal the order exchange-arrival timestamp; in Phase 6 this equals submit timestamp.
- Fill price is `PriceTicks`; no floating-point price is stored in canonical fill or order state.
- Conservation invariant: `sum(fill.quantity for order) == order.filled_quantity`, and `filled_quantity + remaining_quantity == original_quantity`.

## Fee representation / rounding
- Fee rate is a fixed-point integer in parts per million: `fee_rate_ppm`.
- Valid fee rates are `[0, 1,000,000]`.
- Canonical notional unit is `price_ticks * quantity`.
- Fees are calculated per fill as `round_half_up(notional_tick_quantity * fee_rate_ppm / 1,000,000)`.
- Fee calculations use integer arithmetic with checked intermediate range; no hidden floating-point rounding is used.

## Zero-impact book-immutability policy
- Simulated execution reads historical visible liquidity but does not mutate the authoritative historical `OrderBook`.
- Execution uses read-only depth snapshots; historical levels and canonical book hash remain unchanged after simulated fills.
- Market impact is not modeled.

## Limit behavior in Phase 6
- `OrderType::Limit` is represented structurally for future compatibility.
- Phase 6 rejects limit execution explicitly as `New -> Pending -> Rejected`.
- No marketable limit execution, resting limit order, cancel handling, passive queue fill, or queue-ahead model is implemented.

## Strategy callback semantics
- `BookUpdateEvent`: event loop processes the event, applies the update to `OrderBook`, then invokes `Strategy::on_book`.
- `TradeEvent`: event loop preserves source order, does not mutate the book, then invokes `Strategy::on_trade`.
- `InternalEventType::Timer`: `Strategy::on_timer` is invoked for timer events only.
- Callbacks occur once per relevant event in exact event-loop order; same-timestamp market events are not batched.
- Strategy receives `const OrderBook&` and const market event data.

## OrderIntent design
- `OrderIntent` is a strategy-level decision record, not an execution-domain order.
- Fields: `side`, desired `quantity`, `order_type`, optional `limit_price_ticks`, and `decision_timestamp_ns`.
- It has no order ID, exchange-arrival time, acknowledgement, status machine, fill quantity, cancel lifecycle, or execution state.
- Phase 6 conversion through `OrderFactory` creates an `Order`; no fills exist until execution is invoked.

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

## Event ordering policy
- Primary key: `timestamp_ns` ascending.
- At the same timestamp, historical market events are processed before internal scheduled events.
- Among historical market events, Phase 2 source order and `EventKey` ordering are preserved; the event loop does not silently sort or repair market data.
- Among internal events at the same timestamp, deterministic insertion order is preserved by `internal_sequence_id`.

## Book data structure
- Bids: `std::map<PriceTicks, Quantity, std::greater<PriceTicks>>`, best to worst.
- Asks: `std::map<PriceTicks, Quantity, std::less<PriceTicks>>`, best to worst.
- `BookUpdateEvent.quantity` is treated as resulting visible quantity, not an additive delta.

## Canonical hash method
- `canonical_state()` encodes bids best-to-worst, then asks best-to-worst, as `B,<price_ticks>,<quantity>\n` and `A,<price_ticks>,<quantity>\n`.
- `state_hash()` applies FNV-1a 64-bit with offset basis `14695981039346656037` and prime `1099511628211`.
- The hash is a deterministic regression checksum, not a cryptographic claim.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- Phase 7 configurable latency and scheduled exchange arrival are not implemented.
- Phase 8 resting limit orders, cancels, passive fills, and queue-ahead modeling are not implemented.
- Phase 9 portfolio accounting, inventory, cash, PnL, and equity are not implemented.
- No market impact, benchmarking, Python bindings, multithreading, or performance optimization exists yet.

## Next phase
Phase 7 - Latency-Aware Execution

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, and execution_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 7/7 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ ./build/execution_tests
Result: PASS - Phase 6 execution tests passed.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 7/7 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ rg "std::thread|std::mutex|std::atomic|std::async|random_device|system_clock|uuid|wall-clock|resting|queue_ahead|portfolio|PnL|pnl|cash|inventory|passive fill|passive_fill|latency_" include src tests docs CMakeLists.txt
Result: PASS - no Phase 7+ behavior found in execution code; matches are existing prior-phase limitation text, Phase 6 limitation text, Phase 1 latency primitives, and explicit "not implemented" documentation.

$ rg "\bdouble\b|\bfloat\b" include/replay/order.hpp include/replay/fill.hpp include/replay/execution_simulator.hpp src/order.cpp src/fill.cpp src/execution_simulator.cpp tests/unit/execution_simulator_test.cpp
Result: PASS - no floating-point execution price/order/fill state introduced.

$ rg "schedule_internal|InternalEventType::OrderArrival|InternalEventType::CancelArrival|queue_fraction|queue_ahead|Portfolio|class Portfolio|realized|unrealized|equity" include/replay/execution_simulator.hpp src/execution_simulator.cpp tests/unit/execution_simulator_test.cpp docs/execution_model.md
Result: PASS - no Phase 7 scheduling, Phase 8 queue model, or Phase 9 portfolio implementation introduced; only documented current limitations matched.

$ git status --short
Result: PASS - only Phase 6 files are modified/untracked; no staged changes.

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

## Privacy / Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored: PASS
- no private/local-only file staged: PASS
- no staged changes: PASS
- no absolute user-machine paths introduced into public files: PASS
- fixtures are synthetic and publishable: PASS

## Files added/modified
- `CMakeLists.txt`
- `docs/execution_model.md`
- `include/replay/execution_simulator.hpp`
- `include/replay/fill.hpp`
- `include/replay/order.hpp`
- `include/replay/types.hpp`
- `src/execution_simulator.cpp`
- `src/fill.cpp`
- `src/order.cpp`
- `STATUS.md`
- `tests/unit/execution_simulator_test.cpp`
