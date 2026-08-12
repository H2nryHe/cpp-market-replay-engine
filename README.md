# cpp-market-replay-engine

Deterministic C++20 market-data replay engine for normalized L2 book updates and trades. The engine reconstructs a
historical visible order book, dispatches strategy callbacks, routes orders through latency-aware execution, applies
market/limit/passive fills, updates portfolio accounting from fills, and writes reproducible run artifacts.

This is a simulation and infrastructure project. It does not connect to live exchanges, claim exact L3/FIFO queue
reconstruction from L2 data, or optimize a strategy for profitability.

## Build And Test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional sanitizer build where supported:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Quick Start

Run the synthetic end-to-end example from the repository root:

```bash
./build/replay_cli --config configs/example_config.kv --output artifacts/example_run --force
```

The command prints a compact deterministic summary and writes:

- `run_manifest.json`
- `orders.csv`
- `fills.csv`
- `ledger.csv`
- `portfolio_summary.json`
- `metrics.json`

`--force` replaces an existing artifact directory. Without `--force`, the CLI refuses to overwrite a directory that
already contains run artifacts.

## CLI

```bash
replay_cli --config <path> [--output <directory>] [--force]
replay_cli --help
```

The config file is deterministic `key=value` text. Paths are interpreted relative to the current working directory.
`--output` overrides `output_directory` from the config.

Example:

```text
book_updates_path=tests/golden/e2e_book_updates.csv
trades_path=tests/golden/e2e_trades.csv
output_directory=artifacts/example_run
tick_size=0.01
price_format=ticks
strategy_type=scripted
scripted_intents=100:2:buy:market:3:;200:3:buy:limit:6:10000
order_latency_ns=50
cancel_after_arrival_ns=100
queue_fraction_ppm=500000
fee_rate_ppm=100
initial_cash=0
```

`strategy_type=queue_imbalance` uses the demo Queue Imbalance strategy. `strategy_type=scripted` is used by the
synthetic golden fixture to force a compact, manually verifiable integration scenario without changing the demo
strategy.

## Input Format

Book updates CSV:

```text
timestamp_ns,sequence_id,side,price,quantity
100,1,buy,10000,10
```

Trades CSV:

```text
timestamp_ns,sequence_id,price,quantity,aggressor_side
300,4,10000,7,sell
```

`price_format=ticks` treats `price` as integer `PriceTicks`. `price_format=decimal` converts decimal prices using
`tick_size`.

## Architecture

`ReplayEngine` is an orchestration layer. It composes existing components rather than collapsing them:

```text
NormalizedMarketFeed
  -> EventLoop / SimulationClock
  -> historical OrderBook
  -> Strategy
  -> LatencyAwareExecution
  -> Fill
  -> Portfolio
  -> deterministic artifact writer
```

Simulated orders do not mutate the historical `OrderBook`. Market data may mark an existing portfolio position, but
only `Fill` records mutate cash, inventory, lots, fees, turnover, and realized gross PnL.

## Artifacts

`orders.csv` fields:

```text
order_id,side,order_type,original_quantity,filled_quantity,remaining_quantity,limit_price_ticks,
decision_time_ns,submit_time_ns,exchange_arrival_time_ns,final_status
```

`fills.csv` fields:

```text
fill_sequence_id,order_id,timestamp_ns,side,price_ticks,quantity,fee
```

`portfolio_summary.json` records initial cash, ending cash, signed inventory, realized gross PnL, unrealized gross PnL
in doubled units when markable, total fees, net total PnL in doubled units when determinable, turnover, fill count,
ending equity in doubled units, mark availability, and final `mid_x2`.

`metrics.json` records operational counts only: market events, book updates, trades, internal events, strategy intents,
orders by final status, and fills. It intentionally excludes benchmark throughput and performance analytics.

`run_manifest.json` records engine version, input content hashes, canonical config hash, event/order/fill counts,
portfolio summary values, final historical book hash, artifact hashes, and deterministic run hash.

## Deterministic Hashing

Persistent hashes use deterministic FNV-1a over explicit canonical text. They do not use `std::hash`, wall-clock
timestamps, output directory names, absolute input paths, local usernames, or build directories.

The canonical run hash is derived from:

```text
engine_version
input_hash
config_hash
final_book_hash
orders_hash
fills_hash
portfolio_hash
metrics_hash
```

Input hashes are content hashes. Copying identical input data to a different path keeps the same input hash and, if
configuration semantics are otherwise identical, the same run hash.

## Final Mark Policy

Final portfolio marking reuses the accounting policy:

- valid two-sided non-crossed book: mark available using `mid_x2 = best_bid + best_ask`;
- locked book: mark available;
- empty, one-sided, or crossed book with nonzero inventory: mark unavailable;
- flat inventory: cash-only equity remains determinable even without a midpoint.

Half-tick values are preserved in doubled accounting units and are not truncated to integer ticks.

## Synthetic Example

The public golden fixture in `tests/golden/e2e_*.csv` exercises:

- book updates and trades;
- strategy callbacks and two order intents;
- nonzero order latency;
- one aggressive market fill;
- one resting limit order with queue-ahead initialization;
- passive partial fills from trade volume;
- a same-timestamp trade/cancel race where the market event is processed first;
- nonzero fees;
- final portfolio accounting and mark-to-market.

Expected artifacts are checked in under `tests/golden/expected_*`.

## Limitations

- Passive fills from L2 data are queue-depth approximations, not exact FIFO/L3 reconstruction.
- Exchange-specific matching rules, hidden liquidity, iceberg behavior, maker/taker fee differences, market impact,
  margin, leverage, risk limits, and multi-asset allocation are not modeled.
- Benchmarking, profiling, performance optimization, Python bindings, and multithreading are future phases.
