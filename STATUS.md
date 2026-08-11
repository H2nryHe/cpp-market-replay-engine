# Project Status

## Current phase
Phase 8 - Limit Orders, Cancel, Partial Fill & Passive Queue Approximation

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 9/9 passed
- CLI smoke: PASS
- Golden replay: PASS for Phase 3 order-book fixture
- Determinism: PASS for Phase 8 active-order processing, queue states, fills, statuses, remaining quantities, and historical order-book hash

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
- Phase 8 - PASS

## Current work
- Phase 8 completed. Stopped before Phase 9.
- Added marketable limit execution through `LatencyAwareExecution`.
- Added active resting limit-order registry separate from the historical `OrderBook`.
- Added deterministic cancellation for active resting limit orders through `CancelArrival` scheduler events.
- Added fixed-point `QueueFraction` using `queue_fraction_ppm`.
- Added passive trade-driven queue consumption and partial passive fills.
- Added conservative exact-price and trade-through passive fill rules.
- Added trade-volume conservation across our own active orders.
- Preserved Phase 6/7 market-order and latency behavior.
- Added `passive_limit_tests` covering required Phase 8 cases A-AF.
- Updated `docs/execution_model.md` with Phase 8 semantics and fidelity limitations.

## Limit-order arrival semantics
- Buy limit orders sweep asks from lowest upward while `ask_price <= limit_price`.
- Sell limit orders sweep bids from highest downward while `bid_price >= limit_price`.
- Aggressive limit fills use visible historical `PriceTicks` levels and the existing deterministic fee model.
- If the order fully fills aggressively, status becomes `Filled`.
- If an aggressive fill leaves a remainder, status becomes `PartiallyFilled` and the remainder rests at the limit price.
- If no aggressive fill occurs, status remains `Acknowledged` and the full remaining quantity rests.
- Limit execution is available through the latency-aware execution path; the direct Phase 6 `ExecutionSimulator::execute_order` market-order regression behavior remains unchanged.

## Active-order representation
- Active simulated limit orders are stored separately from the historical `OrderBook`.
- Active record fields: `OrderId`, side, limit price, queue-ahead quantity, and arrival timestamp.
- Authoritative `Order` records retain original quantity, filled quantity, remaining quantity, status, timestamps, and limit price.
- Multiple active orders are iterated deterministically by our own order/arrival order.
- This deterministic ordering applies only to simulated orders and is not a claim about historical exchange FIFO position.

## Queue-fraction representation
- `queue_fraction_ppm` is fixed-point parts per million in `[0, 1,000,000]`.
- Examples: `250000 = 0.25`, `500000 = 0.50`, `750000 = 0.75`, `1000000 = 1.00`.
- Invalid fixed-point queue fractions below zero or above one million throw explicitly.
- Initial queue-ahead uses integer `round_half_up(visible_quantity_at_limit_price * queue_fraction_ppm / 1,000,000)`.

## Queue-ahead policy
- For a newly resting order, queue ahead is based on current historical visible quantity at the order's limit price.
- If no visible quantity exists at that price, `queue_ahead = 0`.
- Queue ahead changes only through qualifying reported `TradeEvent` volume.
- `BookUpdateEvent` size reductions, level deletions, quote moves, and spread changes never reduce queue ahead and never create passive fills.
- If a marketable limit order sweeps visible quantity at its own limit price and still has a remainder, that remainder rests with `queue_ahead = 0`.

## Qualifying-trade policy
- Resting Buy limit at `P`: exact-price queue consumption requires Sell aggressor and `trade.price_ticks == P`.
- Resting Sell limit at `P`: exact-price queue consumption requires Buy aggressor and `trade.price_ticks == P`.
- Unknown aggressor side is conservative: no queue reduction and no passive fill.
- Wrong-side aggressor trades do not reduce queue and do not fill.

## Trade-through policy
- Resting Buy at `P`: Sell-aggressor trade below `P` sets queue ahead to zero and may fill at `P`.
- Resting Sell at `P`: Buy-aggressor trade above `P` sets queue ahead to zero and may fill at `P`.
- Trade-through fills still use the simulated order's limit price, not the worse historical trade-through price.

## Passive fill model
- Passive fills from L2 data are queue-depth approximations rather than exact FIFO/L3 order reconstruction.
- Passive fill timestamp equals the qualifying `TradeEvent` timestamp.
- Passive fill price equals the simulated order's limit price.
- Repeated partial passive fills are supported.
- No overfills are allowed.
- Fill conservation is tested across aggressive plus passive fills.

## Cancellation policy
- Cancel requests use the Phase 4 internal scheduler as `InternalEventType::CancelArrival`.
- Default cancel latency is zero; supplied cancel latency uses the same checked `TimestampNs`/`LatencyNs` arithmetic.
- Only active resting limit orders can be canceled.
- `Acknowledged -> Canceled` and `PartiallyFilled -> Canceled` are supported.
- Prior fills and filled quantity are preserved after cancel.
- Canceled orders are removed from active processing and receive no future passive fills.
- Canceling `Filled`, `Canceled`, or `Rejected` orders fails explicitly.

## Same-timestamp race semantics
- Historical market events at timestamp `T` precede internal events at timestamp `T`.
- `TradeEvent @ T` before `CancelArrival @ T`: the trade may fill the order before cancel applies.
- `TradeEvent @ T` before `OrderArrival @ T`: a newly arriving order cannot receive passive fill from that earlier same-timestamp trade.
- These semantics preserve Phase 4 ordering; cancels and order arrivals are not special-cased.

## Trade-volume conservation policy
- For each historical trade, active simulated orders consume reported trade quantity sequentially.
- Queue-ahead consumption occurs before simulated passive fills.
- Total passive fill quantity caused by one trade cannot exceed the trade's reported quantity after modeled queue-ahead consumption.
- Reported trade volume is not duplicated across our own active orders.

## Zero-impact historical-book policy
- Simulated order arrival, resting, passive fill, aggressive fill, and cancel do not mutate the authoritative historical `OrderBook`.
- A simulated buy limit is not inserted into historical bids.
- A simulated sell limit is not inserted into historical asks.
- Historical book hash changes only from historical `BookUpdateEvent` application.
- Market impact is not modeled.

## Timing semantics
- Decision time: when a strategy emits `OrderIntent`.
- Submit time: when the simulator accepts the intent and creates an `Order`.
- Exchange arrival time: `submit_timestamp_ns + configured_latency.count`.
- Decision time and submit time are currently equal.
- Negative user latency is rejected through `LatencyNs` construction helpers.
- Timestamp plus latency overflow throws `std::overflow_error`.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- L2 data cannot identify exact FIFO position, exact queue composition, hidden or iceberg liquidity, or cancellations ahead of our simulated order.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Portfolio accounting, inventory, cash, PnL, equity, turnover, drawdown, and Sharpe are not implemented.
- Market impact, benchmarking, Python bindings, multithreading, and performance optimization are not implemented.

## Next phase
Phase 9 - Portfolio, PnL & Accounting

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, execution_tests, latency_execution_tests, and passive_limit_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 9/9 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ ./build/passive_limit_tests
Result: PASS - Phase 8 passive/limit execution tests passed.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 9/9 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ rg "Portfolio|class Portfolio|cash|inventory|realized|unrealized|equity|turnover|drawdown|Sharpe|returns|PnL|pnl|profitability" include/replay src tests/unit docs CMakeLists.txt
Result: PASS - no Phase 9 accounting implementation introduced; matches are documented limitations or existing non-goal text.

$ rg "std::thread|std::mutex|std::atomic|std::async|random_device|system_clock|uuid|unordered_map|unordered_set" include/replay src tests/unit CMakeLists.txt
Result: PASS - no threading, randomness, wall-clock, UUID, or unordered active-order iteration introduced.

$ rg "\bdouble\b|\bfloat\b" include/replay/order.hpp include/replay/fill.hpp include/replay/execution_simulator.hpp include/replay/latency_execution.hpp src/order.cpp src/fill.cpp src/execution_simulator.cpp src/latency_execution.cpp tests/unit/execution_simulator_test.cpp tests/unit/latency_execution_test.cpp tests/unit/passive_limit_execution_test.cpp
Result: PASS - no floating-point execution price/order/fill or queue-fraction state introduced.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "realistic fills|exact exchange|exact matching|exact queue position|exact FIFO|L3 reconstruction|production exchange|HFT-grade|profitable strategy" README.md docs include/replay src tests/unit CMakeLists.txt
Result: PASS - matches are explicit limitation statements, including the required L2 queue-depth approximation language.

$ git status --short
Result: PASS - only Phase 8 files are modified/untracked; no staged changes.

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

## Phase 8 acceptance gate
- limit orders work: PASS
- cancels work: PASS
- passive partial fills work: PASS
- queue fraction validated: PASS
- L2 limitation explicitly documented: PASS
- tests distinguish trade consumption from ambiguous size changes: PASS

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
- `include/replay/latency_execution.hpp`
- `src/execution_simulator.cpp`
- `src/latency_execution.cpp`
- `STATUS.md`
- `tests/unit/passive_limit_execution_test.cpp`
