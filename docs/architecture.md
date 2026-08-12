# Architecture

`cpp-market-replay-engine` is a single-threaded, deterministic replay engine. The C++ core owns all simulation state and
Python is an optional orchestration layer over the same `ReplayEngine` entry point.

## Core Flow

```text
Market Feed
   |
   v
Deterministic Event Loop
   |
   v
Historical L2 OrderBook
   |
   v
Strategy
   |
   v
OrderIntent
   |
   v
Latency-aware Execution
   |
   v
Aggressive / Passive Fill
   |
   v
Portfolio
   |
   v
Artifacts
```

## Python Boundary

```text
Python research/orchestration
           |
           v
       ReplayEngine
           |
           v
        C++ core
```

The Python module does not reimplement replay, parsing, order-book mutation, execution, or accounting. It constructs or
loads config, calls C++, and exposes immutable result snapshots.

## Component Boundaries

- `MarketFeed` parses normalized public CSV fixtures into validated `MarketEvent` objects.
- `EventLoop` merges historical market events with scheduled internal events using deterministic timestamp and sequence
  ordering.
- `OrderBook` reconstructs visible L2 book state from historical updates. Simulated orders do not mutate this
  authoritative historical state.
- `Strategy` sees read-only market state and emits `OrderIntent` records.
- `LatencyAwareExecution` turns intents into orders at deterministic exchange-arrival timestamps.
- `ExecutionSimulator` creates market, marketable-limit, and passive-limit fills against visible L2 state.
- `Portfolio` mutates only from fills and keeps cash, inventory, FIFO lots, fees, turnover, realized PnL, and marks.
- `ReplayEngine` orchestrates components and writes deterministic artifacts.

## Deliberate Constraints

- The core replay loop is single-threaded.
- Prices use integer ticks; `double` is not used as an order-book price key.
- Passive fills use an L2 queue-depth approximation, not exact FIFO/L3 reconstruction.
- Persistent hashes exclude local paths, output directories, wall-clock timestamps, and usernames.
