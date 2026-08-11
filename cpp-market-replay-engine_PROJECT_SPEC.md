# cpp-market-replay-engine — Project Specification & AI Execution Plan

> **Repository name:** `cpp-market-replay-engine`  
> **Primary language:** C++20  
> **Project type:** Event-driven market-data replay, order-book reconstruction, execution simulation, and performance-engineering system  
> **Primary career target:** Quant Developer / Research Engineer / Market Data / Trading Infrastructure  
> **Document role:** This file is the authoritative implementation specification for an AI coding agent.  
> **Rule:** The agent must read this entire file before modifying the repository.

---

## 0. AI EXECUTION PROTOCOL

### 0.1 Source of truth

This document is the project contract. The coding agent must:

1. Read this file completely before beginning work.
2. Inspect the current repository before making changes.
3. Determine the current phase from `STATUS.md`.
4. Work on **only the current phase** unless the user explicitly authorizes otherwise.
5. Run every test required by that phase.
6. Fix failures before declaring the phase complete.
7. Record commands, test results, benchmark results, known limitations, and files changed in `STATUS.md`.
8. Stop after the phase acceptance gate passes.
9. Never silently weaken a test or change the specification simply to make a phase pass.
10. Never invent benchmark numbers, test results, fills, PnL, or implementation details that were not actually produced.

### 0.2 Required completion format after every phase

At the end of a phase, the agent must report:

```text
Phase:
Status: PASS / FAIL

Implemented:
- ...

Tests executed:
- command
  result

Acceptance gate:
- requirement: PASS/FAIL
- requirement: PASS/FAIL

Files added/modified:
- ...

Known limitations:
- ...

Next phase:
- ...
```

The same information must be appended or updated in `STATUS.md`.

### 0.3 Hard stop rule

**Do not begin the next phase in the same task unless the user explicitly asks for multiple phases.**

A phase is complete only when:

- the project compiles,
- all phase-specific tests pass,
- all pre-existing tests still pass,
- the acceptance gate passes,
- no required behavior is left as a placeholder,
- `STATUS.md` is updated.

### 0.4 Engineering rules

The implementation must prioritize:

1. **Correctness**
2. **Determinism**
3. **Auditability**
4. **Clear architecture**
5. **Performance**
6. **Advanced optimization**

Do **not** optimize before a correct baseline exists.

Do **not** introduce multithreading into the core replay loop before the deterministic single-threaded engine is complete and benchmarked.

Do **not** use floating-point values as order-book price keys.

Do **not** claim exact FIFO queue reconstruction from L2 market data.

Do **not** design the project around discovering a profitable strategy. The primary artifact is the **engine**, not the alpha.

### 0.5 Definition of “done”

A feature is not done merely because it compiles. It must have:

- a defined interface,
- deterministic behavior where applicable,
- tests,
- failure handling,
- documentation when behavior is non-obvious.

---

# 1. PROJECT GOAL

Build a portfolio-quality C++20 market replay engine capable of:

- ingesting historical incremental L2 order-book updates and trades,
- reconstructing the visible order book deterministically,
- preserving causal event ordering,
- exposing an event-driven strategy interface,
- simulating market and limit orders,
- modeling configurable order latency,
- supporting partial fills,
- approximating passive queue fills from L2 data without overstating fidelity,
- maintaining cash, inventory, fees, realized PnL, unrealized PnL, and equity,
- reproducing identical output from identical inputs/configuration,
- producing structured run artifacts,
- benchmarking replay throughput and latency,
- profiling and optimizing measured hot paths,
- optionally exposing the C++ engine to Python through `pybind11`.

The final project should demonstrate that the author can build and test a **performance-sensitive quantitative market infrastructure system**, not merely a strategy notebook rewritten in C++.

---

# 2. NON-GOALS

The following are explicitly out of scope for the first production-quality release:

- live exchange connectivity,
- exchange certification,
- real-money trading,
- exact L3/order-ID reconstruction when only L2 data are available,
- exact exchange matching-engine emulation,
- colocated/HFT latency claims,
- lock-free architecture before the single-threaded engine is proven,
- optimizing a trading signal for profitability,
- large GUI/dashboard work,
- distributed simulation.

A future version may add some of these, but they must not distract from the core engine.

---

# 3. REQUIRED TECHNOLOGY

## 3.1 Core

- C++20
- CMake >= 3.20
- CTest
- Git
- Linux/macOS-compatible build where practical

## 3.2 Testing

Preferred:

- Catch2 **or** GoogleTest
- CTest as the common test entry point

The repository must expose one command equivalent to:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

## 3.3 Quality / diagnostics

Required debug tooling:

- compiler warnings enabled,
- AddressSanitizer where supported,
- UndefinedBehaviorSanitizer where supported.

Recommended:

- clang-format,
- clang-tidy,
- gcov/lcov or LLVM coverage,
- Valgrind on Linux where useful.

## 3.4 Benchmarking

Preferred:

- Google Benchmark, or
- a small deterministic in-repo benchmark harness if dependency simplicity is more important.

Benchmark output must be machine-readable or easy to archive.

## 3.5 Optional Python integration

- `pybind11`

Python bindings are a later phase and must not contaminate the core engine design.

---

# 4. TARGET REPOSITORY STRUCTURE

```text
cpp-market-replay-engine/
├── CMakeLists.txt
├── README.md
├── PROJECT_SPEC.md
├── STATUS.md
├── LICENSE
├── .gitignore
├── cmake/
├── include/
│   └── replay/
│       ├── types.hpp
│       ├── event.hpp
│       ├── market_event.hpp
│       ├── order.hpp
│       ├── fill.hpp
│       ├── order_book.hpp
│       ├── market_feed.hpp
│       ├── simulation_clock.hpp
│       ├── strategy.hpp
│       ├── execution_simulator.hpp
│       ├── portfolio.hpp
│       ├── metrics.hpp
│       └── replay_engine.hpp
├── src/
│   ├── order_book.cpp
│   ├── market_feed.cpp
│   ├── execution_simulator.cpp
│   ├── portfolio.cpp
│   ├── metrics.cpp
│   └── replay_engine.cpp
├── apps/
│   └── replay_cli.cpp
├── strategies/
│   ├── queue_imbalance_strategy.hpp
│   └── queue_imbalance_strategy.cpp
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── golden/
│   └── fixtures/
├── benchmarks/
│   ├── benchmark_order_book.cpp
│   └── benchmark_replay.cpp
├── python/
│   └── bindings.cpp
├── configs/
│   └── example_config.*
├── examples/
├── docs/
│   ├── architecture.md
│   ├── data_contract.md
│   ├── execution_model.md
│   ├── determinism.md
│   └── benchmarks.md
└── artifacts/
    └── .gitkeep
```

The exact split may evolve if there is a clear engineering reason, but the separation of domain types, replay, order book, execution, accounting, tests, benchmarks, and documentation must remain.

---

# 5. CORE DATA MODEL

## 5.1 Price representation

Prices must be represented internally as integer ticks.

Example:

```text
tick_size = 0.01
100.25 -> 10025 ticks
```

Suggested type:

```cpp
using PriceTicks = std::int64_t;
```

No order-book container may use `double` as the price key.

Conversion functions between decimal prices and ticks must be explicit and tested.

## 5.2 Quantity representation

Use an integer representation if the selected dataset can be normalized losslessly to integer lot units.

If fractional quantities are unavoidable, choose a documented fixed-point representation rather than silently relying on binary floating-point equality.

## 5.3 Timestamp

Use integer nanoseconds or another documented integer unit:

```cpp
using TimestampNs = std::uint64_t;
```

All latency arithmetic must use the same unit.

## 5.4 Sequence / causal ordering

Each market event must support a deterministic secondary ordering key such as:

```cpp
struct EventKey {
    TimestampNs timestamp_ns;
    std::uint64_t sequence_id;
};
```

The engine must not assume exchange timestamps are globally unique.

If the source feed contains ordering information that differs from timestamps, preserve the source-observed order.

---

# 6. MARKET EVENT TYPES

Minimum event set:

```text
BookUpdate
Trade
Timer
OrderSubmit
OrderAck
OrderCancel
Fill
```

The external historical feed initially needs only:

```text
BookUpdate
Trade
```

Internal simulator events may generate the remaining types.

Suggested enums:

```cpp
enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Market,
    Limit
};

enum class OrderStatus {
    New,
    Pending,
    Acknowledged,
    PartiallyFilled,
    Filled,
    Canceled,
    Rejected
};
```

---

# 7. MARKET DATA CONTRACT

The engine must consume a normalized historical format independent of any specific exchange.

Minimum L2 update schema:

```text
timestamp_ns
sequence_id
side
price_ticks
quantity
```

Semantics:

- `quantity > 0`: insert/update the visible quantity at the level.
- `quantity == 0`: remove the level.
- updates must be applied in deterministic source order.

Minimum trade schema:

```text
timestamp_ns
sequence_id
price_ticks
quantity
aggressor_side   # if known
```

The engine must document whether the selected dataset represents:

- snapshots + deltas,
- deltas only,
- top-N depth,
- full visible book depth.

If a snapshot is required to initialize replay, this must be explicit.

---

# 8. ORDER-BOOK INVARIANTS

At all times after a valid update sequence:

- bid quantities are non-negative,
- ask quantities are non-negative,
- zero-quantity levels do not remain in the book,
- best bid is the maximum bid price,
- best ask is the minimum ask price,
- if both sides are non-empty, `best_bid < best_ask` unless the input data explicitly represent a crossed state,
- depth queries are deterministic,
- repeated replay of the same updates produces the same state hash.

The engine must decide and document how invalid/crossed input states are handled:

- reject,
- warn and continue,
- or permit as a data-quality event.

Do not silently hide input corruption.

---

# 9. EXECUTION MODEL

## 9.1 Market orders

A market order must consume available opposite-side visible depth in price priority.

Example buy market order:

```text
Ask:
100.01 x 2
100.02 x 3
100.03 x 5

Buy market quantity = 4

Expected:
2 @ 100.01
2 @ 100.02
```

The simulator must support:

- multi-level fills,
- partial fills if visible liquidity is insufficient,
- explicit fees,
- deterministic fill generation.

## 9.2 Limit orders

Limit orders must:

- respect limit price,
- transition through defined lifecycle states,
- allow cancellation,
- permit partial fills.

## 9.3 Latency

Configurable order latency is required.

Conceptually:

```text
strategy emits order at t
exchange arrival = t + configured_latency
```

All historical market events whose event keys occur before the exchange-arrival event must be processed first.

Required test latency values should include at least:

```text
0 us
50 us
100 us
500 us
1 ms
5 ms
```

These values are for experiments; the architecture must accept arbitrary non-negative latency.

## 9.4 Passive fill fidelity

With L2 data, the simulator must **not** claim exact FIFO order placement.

Required model:

### Queue-depth approximation

At order arrival:

```text
queue_ahead = visible_quantity_at_limit_price * queue_fraction
```

As qualifying traded volume consumes that price level:

```text
cumulative_consumed_volume >= queue_ahead
```

the simulated order may begin filling.

Required configuration:

```text
queue_fraction in [0, 1]
```

Recommended experiment values:

```text
0.25
0.50
0.75
1.00
```

`docs/execution_model.md` must clearly state:

> Passive fills from L2 data are queue-depth approximations, not exact FIFO/L3 reconstruction.

---

# 10. ACCOUNTING MODEL

The engine must track at minimum:

- cash,
- signed inventory,
- average entry price or equivalent lot accounting,
- realized PnL,
- unrealized PnL,
- total fees,
- equity / marked portfolio value,
- turnover,
- number of orders,
- number of fills.

Core accounting invariant:

```text
equity = cash + marked_value_of_inventory
```

PnL formulas and sign conventions must be documented and unit tested.

---

# 11. DETERMINISM REQUIREMENT

Given identical:

- normalized input data,
- configuration,
- random seed if randomness is used,
- engine version,

the engine must produce identical:

- fill sequence,
- ending portfolio state,
- order/fill counts,
- deterministic output artifacts,
- canonical hashes.

Recommended hashes:

```text
input_hash
config_hash
fill_log_hash
portfolio_summary_hash
run_hash
```

Benchmark timing is not expected to be bitwise deterministic, but functional outputs are.

---

# 12. TESTING STRATEGY

The project must use several distinct test classes.

## 12.1 Unit tests

Test individual components in isolation.

Examples:

- decimal price -> tick conversion,
- add/update/delete book level,
- best bid/ask,
- market-order depth walking,
- fee calculation,
- PnL calculation,
- order-state transitions.

## 12.2 Integration tests

Test interactions among components.

Examples:

```text
feed -> order book -> strategy -> order -> execution -> fill -> portfolio
```

## 12.3 Golden replay tests

Maintain a tiny, human-verifiable event fixture.

Example:

```text
10-30 market events
```

Expected final outputs are checked into the repo.

Replay must exactly match:

- final book,
- fills,
- portfolio,
- hashes.

Golden tests are the primary regression defense.

## 12.4 Invariant / property tests

Examples:

- quantity never becomes negative,
- filled quantity never exceeds order quantity,
- canceled orders cannot later fill unless cancellation-race semantics explicitly permit it,
- a fully filled order cannot return to pending,
- identical replay produces identical result hash,
- total filled quantity equals sum of individual fills,
- accounting equation holds after every fill.

## 12.5 Error-path tests

Examples:

- malformed row,
- invalid side,
- negative normalized quantity,
- impossible tick conversion,
- non-monotonic sequence within a stream where monotonicity is required,
- missing initialization snapshot,
- invalid queue fraction,
- negative latency.

## 12.6 Sanitizer tests

Debug/CI should support:

```text
AddressSanitizer
UndefinedBehaviorSanitizer
```

A phase involving pointer/container changes cannot pass if sanitizer tests fail.

## 12.7 Benchmark regression

After a stable baseline exists, save benchmark results.

Optimization phases must compare before vs after under the same:

- compiler,
- optimization flags,
- hardware,
- input fixture,
- benchmark configuration.

Never compare incomparable benchmark runs.

---

# 13. PHASE PLAN

---

# PHASE 0 — Repository Bootstrap

## Goal

Create a clean, reproducible C++20 project that builds and runs tests.

## Requirements

Create:

- CMake project,
- library target,
- CLI target,
- test target,
- warning flags,
- debug sanitizer option,
- `.gitignore`,
- `README.md`,
- `STATUS.md`,
- `PROJECT_SPEC.md`,
- base directory structure.

Required CMake properties:

```text
C++20 required
no compiler extensions required
warnings enabled
```

Recommended warnings:

GCC/Clang:

```text
-Wall
-Wextra
-Wpedantic
-Wconversion
-Wshadow
```

Do not use `-Werror` by default if it harms cross-platform compilation; CI may enable it selectively.

## Tests

### Build test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Expected: success.

### CTest smoke test

```bash
ctest --test-dir build --output-on-failure
```

Expected: at least one real smoke test passes.

### CLI smoke test

```bash
./build/.../replay_cli --help
```

Expected: exit 0 and print usage.

### Sanitizer build

If supported:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Expected: no sanitizer failure.

## Acceptance gate

- [ ] clean configure
- [ ] clean build
- [ ] CTest passes
- [ ] CLI `--help` works
- [ ] sanitizer build works where supported
- [ ] `STATUS.md` records exact commands/results

---

# PHASE 1 — Core Domain Types

## Goal

Define explicit, strongly typed domain primitives.

## Requirements

Implement:

- `TimestampNs`
- `PriceTicks`
- quantity representation
- `Side`
- `OrderType`
- `OrderStatus`
- event key
- price conversion helper
- latency representation
- validation helpers

No order-book implementation yet.

## Tests

### Price conversion

Examples:

```text
tick_size 0.01:
100.25 -> 10025
100.00 -> 10000
```

Test invalid values and rounding policy.

### Side parsing

Valid:

```text
buy
sell
B
S
```

Only support aliases explicitly documented.

Invalid input must fail clearly.

### Event ordering

Construct events with:

- same timestamp, different sequence,
- different timestamps.

Verify deterministic strict ordering.

### Invalid values

Verify rejection of:

- negative latency,
- invalid tick size,
- invalid normalized quantity,
- invalid enums from parser input.

## Acceptance gate

- [ ] no market price key uses `double`
- [ ] event ordering has deterministic tests
- [ ] conversion rules documented
- [ ] all unit tests pass
- [ ] sanitizer tests pass

---

# PHASE 2 — Market Feed & Normalized Parser

## Goal

Read a tiny normalized historical fixture and emit deterministic market events.

## Requirements

Implement:

- normalized L2 update parser,
- normalized trade parser,
- validation,
- deterministic event stream,
- clear parse errors with line/context,
- tiny test fixtures in `tests/fixtures/`.

Do not optimize parsing yet.

## Tests

### Valid fixture

Read known fixture.

Verify:

- exact event count,
- exact event types,
- exact timestamps,
- exact sequence IDs,
- exact price ticks,
- exact quantities.

### Malformed input

Test:

- missing column,
- malformed integer,
- invalid side,
- invalid quantity,
- invalid event type.

Expected: explicit failure, not undefined behavior.

### Ordering test

Feed events with timestamp ties.

Verify output preserves the required causal secondary ordering.

### Round-trip / canonicalization test

If the project supports normalized serialization:

```text
parse -> serialize -> parse
```

must preserve canonical event content.

## Acceptance gate

- [ ] parser handles valid fixture
- [ ] parser rejects malformed fixture
- [ ] deterministic ordering confirmed
- [ ] errors are actionable
- [ ] no premature optimization

---

# PHASE 3 — L2 Order Book Reconstruction

## Goal

Build the first core engine component: deterministic visible order-book replay.

## Requirements

Support:

- bid add/update,
- ask add/update,
- bid delete,
- ask delete,
- best bid,
- best ask,
- spread,
- midprice,
- depth query,
- top-N snapshot,
- book state hash.

Baseline data structure may use ordered standard-library containers.

Correctness is more important than speed.

## Tests

### Add/update/delete

Fixture:

```text
bid 100.00 x 5
bid 99.99  x 3
ask 100.01 x 4
ask 100.02 x 8
```

Verify best prices and sizes.

Modify quantity and verify state.

Set quantity to zero and verify level disappears.

### Price priority

Verify:

```text
best_bid = maximum bid
best_ask = minimum ask
```

### Spread / mid

For:

```text
bid = 10000 ticks
ask = 10002 ticks
```

verify spread and mid representation according to documented arithmetic.

### Empty side

Verify safe behavior when:

- no bids,
- no asks,
- both sides empty.

### Crossed/invalid book policy

Feed a crossed state.

Verify behavior matches documented policy.

### Golden book replay

Replay a small deterministic sequence and compare:

- top levels,
- level counts,
- final state hash.

### Repeatability

Run same sequence 100 times.

Expected: identical final state/hash every run.

## Acceptance gate

- [ ] all book operations correct
- [ ] edge cases tested
- [ ] golden replay passes
- [ ] deterministic state hash stable
- [ ] no negative quantities
- [ ] sanitizer tests pass

---

# PHASE 4 — Simulation Clock & Deterministic Event Loop

## Goal

Create the single-threaded event-driven replay kernel.

## Requirements

Implement:

- simulation clock,
- event scheduler / queue,
- deterministic processing,
- market event dispatch,
- internal scheduled events,
- end-of-stream handling.

The replay kernel must remain single-threaded.

## Required ordering rule

The code must explicitly define how ties are broken between:

```text
historical market event
order arrival
cancel arrival
timer event
```

Tie-breaking must be documented and tested.

## Tests

### Clock monotonicity

The simulation clock must never move backward.

### Timestamp ties

Construct multiple event classes at one timestamp.

Verify exact expected processing order.

### Scheduled internal event

At `t=100`, schedule an order-arrival event for `t=150`.

Feed historical events at:

```text
120
149
151
```

Expected order:

```text
120 market
149 market
150 order arrival
151 market
```

### Deterministic event trace

For a small fixture, emit a trace of processed event IDs.

Repeat 100 times.

Expected: identical trace/hash.

## Acceptance gate

- [ ] event loop is single-threaded
- [ ] tie-breaking documented
- [ ] internal events interleave correctly with market events
- [ ] deterministic trace test passes
- [ ] no later phase logic embedded prematurely

---

# PHASE 5 — Strategy Interface + Queue Imbalance Demo

## Goal

Expose a clean event-driven strategy API without coupling strategy logic to engine internals.

## Requirements

Minimum callbacks:

```cpp
on_book(...)
on_trade(...)
on_timer(...)
```

Strategy receives read-only market state.

Strategy emits order intents through an explicit interface.

Implement one deliberately simple example:

```text
Queue Imbalance Strategy
```

Suggested feature:

```text
QI = (bid_volume - ask_volume) / (bid_volume + ask_volume)
```

The exact volume depth used must be configurable/documented.

This strategy is a **demo harness**, not the core contribution.

## Tests

### Callback test

Verify strategy receives callbacks in exact event order.

### Read-only state test

Strategy cannot directly mutate the order book.

### Deterministic signal test

Given known book states, verify expected:

```text
buy / sell / no action
```

### Zero denominator

If total selected depth is zero, behavior must be explicitly defined and tested.

### No lookahead

Verify strategy at event N cannot access event N+1.

## Acceptance gate

- [ ] strategy interface cleanly separated
- [ ] QI demo works
- [ ] no direct order-book mutation
- [ ] no future-event access
- [ ] strategy behavior unit tested

---

# PHASE 6 — Order Lifecycle + Market Execution

## Goal

Implement order objects and correct market-order execution against visible L2 depth.

## Requirements

Implement:

- unique order ID,
- submit timestamp,
- exchange-arrival timestamp,
- side,
- quantity,
- type,
- limit price where applicable,
- filled quantity,
- remaining quantity,
- status transitions,
- fill records,
- fee model.

Market orders must walk opposite-side depth.

## Tests

### One-level fill

Ask:

```text
100.01 x 5
```

Buy market 3.

Expected:

```text
3 @ 100.01
status = Filled
```

### Multi-level fill

Ask:

```text
100.01 x 2
100.02 x 3
100.03 x 5
```

Buy market 4.

Expected:

```text
2 @ 100.01
2 @ 100.02
```

### Insufficient liquidity

Visible ask quantity = 5.

Buy market quantity = 8.

Expected:

```text
filled = 5
remaining = 3
```

Final status policy must be documented.

### Fee test

Verify exact fee calculation for known notional and fee rate.

### Lifecycle validity

Reject invalid transitions such as:

```text
Filled -> Pending
Canceled -> Filled
```

unless a documented race model explicitly allows otherwise.

### Fill conservation

```text
sum(fill quantities) == order filled quantity
filled quantity <= order quantity
```

## Acceptance gate

- [ ] market orders walk depth correctly
- [ ] partial execution supported
- [ ] fees deterministic
- [ ] lifecycle transition table documented
- [ ] fill invariants tested

---

# PHASE 7 — Latency-Aware Execution

## Goal

Make execution sensitive to market events occurring between strategy decision time and exchange arrival.

## Requirements

For every order:

```text
arrival_time = submit_time + configured_latency
```

The engine must process intervening market events first.

Latency must be configurable without recompilation.

## Tests

### Zero latency

Signal and order arrival at same timestamp according to documented tie-breaking.

Expected behavior must be deterministic.

### Book moves during latency

At `t=100`:

```text
strategy sends buy market
latency = 100
```

At `t=150`:

```text
ask 100.01 removed
ask 100.05 added
```

At `t=200`, order arrives.

Expected: fill against market state at `t=200`, not state at `t=100`.

### Latency sweep correctness

Run:

```text
0 us
50 us
100 us
500 us
1 ms
5 ms
```

Verify:

- configuration accepted,
- resulting arrival times correct,
- deterministic output per configuration.

### Negative latency

Must fail validation.

## Acceptance gate

- [ ] no execution uses stale decision-time book by accident
- [ ] intervening events processed correctly
- [ ] latency configuration works
- [ ] deterministic tests pass

---

# PHASE 8 — Limit Orders, Cancel, Partial Fill & Passive Queue Approximation

## Goal

Support resting limit-order simulation with an explicit L2 queue model.

## Requirements

Implement:

- limit order submission,
- non-marketable resting order,
- marketable limit order,
- cancel request,
- partial fill,
- queue-depth approximation,
- configurable `queue_fraction`.

Required documentation must explicitly state the L2 limitation.

## Queue model

At arrival:

```text
queue_ahead = visible_quantity_at_price * queue_fraction
```

Qualifying consumption at that level reduces queue ahead.

Only after the modeled queue ahead is exhausted may the simulated order begin to fill.

The implementation must clearly define how book-size reductions vs reported trades are interpreted.

Do not silently assume every size reduction is a trade.

## Tests

### Resting order

Place buy limit below best ask.

Expected: order rests, no immediate fill.

### Marketable limit

Place buy limit above/equal eligible ask prices.

Expected: executes only at prices satisfying the limit.

### Cancel before fill

Submit and acknowledge order, then cancel before qualifying volume.

Expected: no later fill.

### Partial passive fill

Construct known queue-ahead and trade sequence.

Verify exact partial fill quantities.

### Queue fraction

Run same fixture with:

```text
0.25
0.50
0.75
1.00
```

Expected: fill timing changes monotonically according to modeled queue depth.

### Invalid queue fraction

Reject:

```text
< 0
> 1
```

### L2 fidelity test

Documentation test/check should ensure README/docs never describe the model as:

```text
exact FIFO
exact L3 reconstruction
```

## Acceptance gate

- [ ] limit orders work
- [ ] cancels work
- [ ] passive partial fills work
- [ ] queue fraction validated
- [ ] L2 limitation explicitly documented
- [ ] tests distinguish trade consumption from ambiguous size changes

---

# PHASE 9 — Portfolio, PnL & Accounting

## Goal

Build independent, auditable portfolio accounting.

## Requirements

Track:

- cash,
- signed inventory,
- cost basis / average entry,
- realized PnL,
- unrealized PnL,
- fees,
- equity,
- turnover.

The portfolio must update only from fills, not from strategy intents.

## Tests

### Simple round trip

```text
buy 100 @ 10
sell 100 @ 11
fees = 0
```

Expected:

```text
inventory = 0
realized pnl = 100
```

### Fees

Same trade with deterministic fees.

Expected:

```text
net pnl = gross pnl - fees
```

### Partial close

Example:

```text
buy 100 @ 10
sell 40 @ 11
```

Verify:

- remaining inventory,
- realized PnL,
- remaining cost basis.

### Short position

Test:

```text
sell short
buy to cover
```

Verify sign conventions.

### Mark-to-market

With open inventory and known midprice:

```text
equity = cash + inventory * mark
```

### Invariant after every fill

Check accounting equality after each fill in a sequence.

### Fill-only mutation

Submitting/canceling an unfilled order must not change cash/inventory.

## Acceptance gate

- [ ] long accounting works
- [ ] short accounting works
- [ ] partial close works
- [ ] fees incorporated
- [ ] equity invariant holds
- [ ] accounting is fill-driven only

---

# PHASE 10 — End-to-End Replay Engine, CLI & Artifacts

## Goal

Connect all core components into a reproducible command-line simulation.

## Required pipeline

```text
normalized feed
    ->
event loop
    ->
order book
    ->
strategy
    ->
orders
    ->
latency-aware execution
    ->
fills
    ->
portfolio
    ->
metrics/artifacts
```

## CLI

Minimum usage concept:

```bash
replay_cli \
  --book-data path \
  --trade-data path \
  --config configs/example_config.* \
  --output artifacts/run_name/
```

Exact flag design may differ if documented.

## Required artifacts

Each run should emit deterministic data such as:

```text
run_manifest.*
orders.*
fills.*
portfolio_summary.*
metrics.*
```

Manifest should include:

- engine version / git commit if available,
- input hash,
- config hash,
- start/end timestamps,
- event count,
- order count,
- fill count,
- ending inventory,
- PnL summary,
- run hash.

## Tests

### Tiny end-to-end golden run

Use a human-verifiable fixture.

Assert exact:

- processed event count,
- orders,
- fills,
- final inventory,
- cash,
- PnL,
- output hash.

### Repeatability

Run same simulation multiple times.

Expected: functional artifact hashes identical.

### Config sensitivity

Change only latency.

Expected:

- config hash changes,
- run hash changes if outcomes change,
- input hash remains constant.

### Bad input path

Expected: non-zero exit and useful error.

### Output overwrite policy

Behavior must be explicit:

- refuse,
- version,
- or require force flag.

Test whichever policy is chosen.

## Acceptance gate

- [ ] complete CLI run succeeds
- [ ] golden E2E test passes
- [ ] deterministic artifacts produced
- [ ] manifest contains required metadata
- [ ] failure modes are clear

---

# PHASE 11 — Benchmark Baseline & Profiling

## Goal

Measure the correct baseline before optimization.

## Requirements

Create reproducible benchmarks for:

1. order-book update throughput,
2. full replay throughput,
3. event-processing latency,
4. memory usage if practical.

Required primary metrics:

```text
events/sec
ns/event
```

Recommended:

```text
p50
p95
p99
peak RSS
```

The benchmark report must record:

- CPU model,
- OS,
- compiler,
- compiler version,
- build flags,
- dataset/fixture,
- event count,
- date/time,
- git commit.

## Benchmark sizes

At least:

```text
small correctness fixture
100K events
1M events
```

If hardware/data permit:

```text
10M+ events
```

## Tests

### Benchmark reproducibility

Run benchmark at least 3 times.

Report median; do not cherry-pick best run.

### Functional checksum

Every benchmark run must verify the functional state hash so speed changes cannot hide incorrect replay.

### Release build

Benchmarks must use an optimized build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Acceptance gate

- [ ] baseline benchmark exists
- [ ] methodology documented
- [ ] 3+ runs collected
- [ ] median reported
- [ ] functional checksum verified
- [ ] no optimization claim yet unless before/after comparison exists

---

# PHASE 12 — Profile-Guided Optimization

## Goal

Improve measured hot paths while preserving exact behavior.

## Rules

Optimization must follow:

```text
measure -> identify hotspot -> change -> test -> benchmark -> compare
```

Potential tools/changes:

- `reserve()`,
- reduce allocations,
- contiguous storage,
- preallocated event buffers,
- cheaper lookup structure where appropriate,
- reduce copying,
- move semantics,
- data-layout improvements,
- object pools only if profiling justifies them.

Do not introduce complexity without evidence.

## Required before/after report

Example format:

```text
Baseline:
X.XX M events/sec

Optimized:
Y.YY M events/sec

Speedup:
Y.YY / X.XX = Z.ZZx

Functional hash:
identical
```

## Tests

### Full regression

```bash
ctest --test-dir build --output-on-failure
```

### Sanitizers

Run debug sanitizer suite.

### Golden hashes

All golden output hashes must remain unchanged unless a deliberate, documented semantic change occurred.

### Benchmark comparison

Same:

- machine,
- dataset,
- compiler,
- flags.

Collect 3+ runs for each relevant version/configuration.

## Acceptance gate

- [ ] optimization addresses measured hotspot
- [ ] all correctness tests pass
- [ ] sanitizers pass
- [ ] functional hash preserved
- [ ] benchmark comparison fair
- [ ] improvement documented honestly
- [ ] regression or no-gain changes may be reverted

---

# PHASE 13 — Python Binding with pybind11

## Goal

Demonstrate a realistic research architecture where Python controls analysis and C++ performs replay/simulation.

## Requirements

Expose a minimal stable API, for example:

```python
from market_replay import ReplayEngine

engine = ReplayEngine(config)
result = engine.run(...)
```

Python should be able to access:

- summary metrics,
- fills,
- ending portfolio state,
- optionally book/replay metadata.

Do not expose every internal class unnecessarily.

## Tests

### Import test

```bash
python -c "import market_replay"
```

Expected: success.

### C++ vs Python equivalence

Run same fixture through:

- C++ CLI,
- Python binding.

Expected identical:

- event count,
- fills,
- portfolio,
- run hash.

### Invalid config

Verify Python receives a clear exception rather than process crash.

### Repeated run

Repeated Python calls must remain deterministic.

## Acceptance gate

- [ ] Python module imports
- [ ] core replay works
- [ ] outputs match CLI
- [ ] errors translate safely
- [ ] bindings remain a thin layer over C++ core

---

# PHASE 14 — CI, Documentation & Portfolio Release

## Goal

Turn the repository from “working code” into a reviewer-friendly public portfolio project.

## CI requirements

At minimum:

```text
configure
build
unit/integration/golden tests
sanitizer job where practical
```

Recommended matrix:

```text
Linux GCC
Linux Clang
```

macOS optional.

Do not run unstable long benchmarks on every PR. A benchmark smoke test is sufficient in CI; full benchmarks can be manual/release artifacts.

## README requirements

README must include:

1. project purpose,
2. architecture,
3. key engineering decisions,
4. build instructions,
5. quick-start example,
6. data contract,
7. execution-model assumptions,
8. L2 passive-fill limitation,
9. determinism approach,
10. testing strategy,
11. benchmark methodology,
12. actual measured benchmark table,
13. example run artifacts,
14. limitations / non-goals,
15. future work.

## Required docs

```text
docs/architecture.md
docs/data_contract.md
docs/execution_model.md
docs/determinism.md
docs/benchmarks.md
```

## Portfolio claim discipline

Allowed only if actually measured:

```text
processed X million events
Y million events/sec
Z ns/event
A.x speedup
```

Never fabricate or extrapolate.

Do not claim:

```text
production exchange engine
HFT-grade
exact FIFO from L2
profitable strategy
live trading system
```

unless future work truly supports those claims.

## Acceptance gate

- [ ] clean clone can build from documented instructions
- [ ] CI green
- [ ] tests green
- [ ] README complete
- [ ] benchmark numbers reproducible and sourced
- [ ] limitations explicit
- [ ] release tag can be created

---

# 14. OPTIONAL PHASE 15 — Controlled Multithreading Experiment

This phase is optional and must **not** begin until Phases 0-14 are stable.

## Goal

Explore concurrency without compromising deterministic core behavior.

A reasonable architecture:

```text
feed parser thread
    ->
SPSC queue
    ->
single simulation/replay thread
```

The authoritative state mutation loop remains single-threaded.

## Requirements

Compare:

```text
single-thread baseline
vs
producer/consumer ingestion
```

Do not claim latency improvement unless measured.

## Tests

- ThreadSanitizer if supported.
- Same functional replay hash.
- No event loss.
- No event duplication.
- Exact sequence preservation.
- Repeated runs deterministic.
- Fair benchmark comparison.

## Acceptance gate

- [ ] TSan clean where supported
- [ ] functional hash identical
- [ ] no event loss/reordering
- [ ] measured performance result documented
- [ ] concurrency retained only if justified

---

# 15. CROSS-PHASE ACCEPTANCE TEST MATRIX

| Area | Required Test |
|---|---|
| Build | clean Debug + Release builds |
| Unit | component-level CTest suite |
| Integration | feed -> replay -> execution -> portfolio |
| Golden | exact tiny replay outputs |
| Determinism | repeated replay produces same hashes |
| Invariants | quantities, fills, lifecycle, accounting |
| Error handling | malformed/invalid input |
| Memory safety | ASan |
| Undefined behavior | UBSan |
| Concurrency, if added | TSan |
| Performance | repeatable Release benchmark |
| Optimization | before/after + identical functional hash |
| Python | C++/Python result equivalence |
| CI | clean environment build/test |

---

# 16. REQUIRED GOLDEN FIXTURE

Create one tiny canonical scenario that is simple enough to verify manually.

It should include:

- initial bid/ask levels,
- at least one book update,
- at least one trade,
- at least one strategy signal,
- one market order,
- one resting limit order,
- one cancel,
- one partial fill,
- non-zero latency,
- non-zero fee,
- final non-trivial portfolio state.

Store:

```text
tests/golden/input_*
tests/golden/expected_orders.*
tests/golden/expected_fills.*
tests/golden/expected_portfolio.*
tests/golden/expected_manifest.*
```

A semantic engine change must either preserve these outputs or explicitly update them with a documented reason.

---

# 17. FAILURE-INJECTION TESTS

Before public release, deliberately test at least:

```text
empty input
single event
one-sided book
duplicate sequence
out-of-order input
malformed row
zero quantity
huge quantity near representation limit
large price ticks
market order with empty opposite book
cancel unknown order ID
duplicate cancel
fill after terminal state
invalid queue fraction
invalid latency
unwritable artifact directory
```

The engine must fail clearly rather than corrupting state.

---

# 18. PERFORMANCE EXPERIMENT PLAN

The final benchmark report should answer:

## 18.1 Order-book representation

Compare, where practical:

```text
baseline ordered map
vs
optimized representation
```

Only implement alternatives after profiling.

Record:

```text
update throughput
best bid/ask lookup cost
memory
```

## 18.2 Replay scale

Measure:

```text
100K
1M
10M+ if practical
```

Report whether throughput is stable with scale.

## 18.3 Latency experiment

For a fixed replay and strategy:

```text
0 us
50 us
100 us
500 us
1 ms
5 ms
```

Report:

```text
fill count/rate
average execution price
fees
PnL
inventory
```

This is an execution-sensitivity experiment, **not** proof of a profitable strategy.

## 18.4 Queue fraction experiment

Run:

```text
0.25
0.50
0.75
1.00
```

Report sensitivity of passive fills and outcomes.

---

# 19. ARCHITECTURE PRINCIPLES

## 19.1 Separation of concerns

The following must remain conceptually separate:

```text
MarketFeed
OrderBook
SimulationClock
Strategy
ExecutionSimulator
Portfolio
Metrics
ReplayEngine
```

Avoid a giant `Engine` class that owns all logic.

## 19.2 Dependency direction

Preferred flow:

```text
ReplayEngine orchestrates components.
Components do not reach globally into each other.
```

Strategy should not mutate:

```text
OrderBook
Portfolio
Execution internals
```

It should emit order intents.

Portfolio should react to fills, not market feed directly except for mark-to-market pricing through an explicit interface.

## 19.3 Testability

Favor dependency injection or explicit construction over hidden singletons/global state.

## 19.4 Deterministic randomness

If future fill models use stochastic behavior:

- seed must be explicit,
- seed must appear in run manifest,
- tests must use fixed seeds.

---

# 20. CODING STANDARDS

Use:

- RAII,
- value semantics where appropriate,
- `const` correctness,
- `enum class`,
- `std::optional` for genuinely optional values,
- smart pointers only where ownership requires them,
- standard library before custom containers,
- clear error messages,
- documented units in identifiers/types.

Avoid:

- raw owning pointers,
- hidden global mutable state,
- premature templates,
- macro-heavy design,
- “clever” undefined behavior,
- unnecessary inheritance,
- silent numeric narrowing.

Prefer descriptive names such as:

```text
timestamp_ns
price_ticks
remaining_quantity
exchange_arrival_time_ns
```

rather than ambiguous names such as:

```text
t
p
q
x
```

inside public interfaces.

---

# 21. REVIEW CHECKLIST FOR EVERY PULL REQUEST / AI PHASE

Before declaring work complete, answer:

```text
[ ] Does it compile in Debug?
[ ] Does it compile in Release?
[ ] Do all existing tests pass?
[ ] Are new behaviors tested?
[ ] Are failure paths tested?
[ ] Is deterministic behavior preserved?
[ ] Do sanitizer tests pass where supported?
[ ] Did I avoid weakening an existing test?
[ ] Did I update STATUS.md?
[ ] Did I avoid unsupported performance claims?
[ ] Did I stay inside the current phase scope?
```

For performance changes also answer:

```text
[ ] Was a hotspot measured first?
[ ] Is the comparison on the same machine/build/data?
[ ] Are there at least 3 runs?
[ ] Is the reported number a median or clearly labeled statistic?
[ ] Is the functional output hash unchanged?
```

---

# 22. STATUS.md TEMPLATE

`STATUS.md` should always contain a compact project-state summary.

Recommended format:

```markdown
# Project Status

## Current phase
Phase X — Name

## Phase status
IN PROGRESS / PASS / FAIL

## Last verified commit
<commit>

## Build
- Debug: PASS/FAIL
- Release: PASS/FAIL
- ASan/UBSan: PASS/FAIL/UNSUPPORTED

## Tests
- CTest: X/X passed
- Golden replay: PASS/FAIL
- Determinism: PASS/FAIL

## Benchmarks
Not started / latest verified results

## Completed phases
- Phase 0 — PASS
- ...

## Current work
- ...

## Known limitations
- ...

## Next phase
Phase X+1 — ...

## Verification commands
```bash
...
```
```

The AI agent must update this file after each phase.

---

# 23. FINAL DEMONSTRATION SCENARIO

The final public release should support a command that demonstrates:

```text
1. load normalized L2 + trades
2. reconstruct book
3. calculate queue imbalance
4. generate order intents
5. apply configurable latency
6. simulate market/passive fills
7. update portfolio
8. write artifacts
9. print deterministic run summary
```

Example terminal summary shape:

```text
cpp-market-replay-engine

Events processed:      ...
Book updates:          ...
Trades:                ...
Orders submitted:      ...
Fills:                 ...
Ending inventory:      ...
Gross PnL:             ...
Fees:                  ...
Net PnL:               ...
Replay throughput:     ... events/sec
Run hash:              ...
```

Only print metrics actually implemented and measured.

---

# 24. FINAL RESUME-QUALITY SUCCESS CRITERIA

The project is considered portfolio-ready only when the repository supports defensible statements similar to the following.

Do **not** pre-fill numbers. Replace placeholders only with measured results.

### Potential bullet 1 — Architecture

> Built a deterministic event-driven C++20 market simulator that reconstructs L2 order books from incremental updates and routes strategy orders through latency-aware execution and portfolio accounting.

### Potential bullet 2 — Execution

> Implemented integer-tick pricing, market/limit orders, partial fills, explicit fees, configurable latency, and queue-depth-based passive execution while documenting the fidelity limits of L2 data.

### Potential bullet 3 — Performance

> Profiled and optimized replay hot paths, improving throughput from **X.XM to Y.YM events/sec** while preserving exact golden-replay and functional output hashes.

### Potential bullet 4 — Python integration

> Exposed the C++ replay core to Python through pybind11, separating performance-sensitive simulation from research, visualization, and parameter-sweep workflows.

A resume bullet may be used only after the underlying result has actually been implemented and verified.

---

# 25. INTERVIEW-READINESS REQUIREMENTS

Before the project is considered complete, the author should be able to explain:

1. Why use integer ticks instead of `double` prices?
2. How are timestamp ties resolved?
3. Why is the core engine single-threaded?
4. How does a market order walk the book?
5. What happens if liquidity is insufficient?
6. How does latency alter execution?
7. Why can L2 data not reconstruct exact FIFO queue position?
8. How does the queue-depth approximation work?
9. How are partial fills represented?
10. What are the valid order-state transitions?
11. How is PnL accounted for?
12. What invariants catch accounting bugs?
13. How is replay determinism verified?
14. What was the initial performance bottleneck?
15. Which optimization changed performance and why?
16. Why is the optimized implementation still correct?
17. What belongs in C++ versus Python?
18. What assumptions prevent this from being called a real exchange matching engine?
19. What limitations would matter in live trading?
20. What would be the next engineering improvement?

---

# 26. PROJECT NARRATIVE

This repository should tell one coherent story:

```text
Correct historical market-data replay
        ↓
Deterministic L2 order-book reconstruction
        ↓
Event-driven strategy callbacks
        ↓
Latency-aware order lifecycle and execution
        ↓
Conservative L2 passive-fill modeling
        ↓
Auditable portfolio accounting
        ↓
Golden tests + determinism + sanitizers
        ↓
Profiling
        ↓
Measured C++ optimization
        ↓
Optional Python research interface
```

The project is successful when a reviewer sees evidence of:

```text
C++ engineering
+
market microstructure understanding
+
data correctness
+
simulation design
+
testing discipline
+
performance measurement
+
quantitative judgment
```

—not merely a backtest with a trading strategy.

---

# 27. FIRST ACTION FOR ANY AI CODING AGENT

Before writing code:

1. Read this entire specification.
2. Inspect all existing repository files.
3. Read `STATUS.md`.
4. Identify the first incomplete phase.
5. Restate that phase's goal and acceptance gate internally.
6. Implement only that phase.
7. Run its complete test plan.
8. Update `STATUS.md`.
9. Stop and report results.

**Never skip directly to later features because they appear more interesting.**

---

# 28. RELEASE DEFINITION

The first portfolio release should be tagged only after:

```text
Phases 0-14: PASS
```

Phase 15 multithreading is optional.

Suggested release:

```text
v1.0.0
```

Release notes should summarize:

- supported market data,
- execution features,
- deterministic guarantees,
- test coverage,
- benchmark environment,
- measured performance,
- known limitations.

---

# 29. FINAL SAFETY / CLAIM-DISCIPLINE RULES

The AI agent must protect the technical credibility of this project.

Never write unsupported statements such as:

```text
HFT-grade
production exchange engine
nanosecond trading system
exact FIFO matching
institutional execution model
profitable strategy
realistic fills
```

Prefer precise language:

```text
event-driven
deterministic
production-style
L2 replay
queue-depth approximation
latency-aware simulation
measured replay throughput
reproducible artifacts
```

When fidelity is uncertain, document the assumption rather than hiding it.

**Engineering credibility is more valuable than aggressive claims.**
