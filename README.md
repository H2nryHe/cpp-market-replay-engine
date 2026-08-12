# cpp-market-replay-engine

Deterministic C++20 event-driven market replay and execution engine for normalized L2 book updates and trades. It
reconstructs visible order books, preserves causal event ordering, routes strategy intents through latency-aware
market/limit execution, applies an explicit L2 passive-fill approximation, maintains FIFO portfolio accounting, and
writes reproducible artifacts.

## Highlights

- C++20 event-driven replay core with CMake and CTest.
- Deterministic L2 order-book reconstruction with integer tick prices.
- Timestamp plus sequence ordering for same-timestamp market events.
- Latency-aware market and limit order lifecycle.
- Passive fills from L2 queue-depth approximation, not exact FIFO/L3 reconstruction.
- FIFO portfolio accounting for cash, inventory, fees, turnover, realized PnL, and exact half-tick marks.
- Reproducible run artifacts and golden hashes.
- pybind11 `market_replay` Python interface for research orchestration over the C++ core.
- Measured 1.49x replay hot-path optimization: 24.64M to 36.65M events/sec.
- Current documented 1M-event single-threaded core benchmark: 36.6M events/sec, 27.287 ns/event.

This is a simulation and infrastructure project. It does not connect to live exchanges, emulate exact exchange matching,
claim exact L3/FIFO queue reconstruction from L2 data, or optimize a strategy for profitability.

## Quick Start

```bash
git clone https://github.com/H2nryHe/cpp-market-replay-engine.git
cd cpp-market-replay-engine

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

./build/replay_cli --config configs/example_config.kv --output artifacts/example_run --force
```

Expected synthetic example summary:

```text
Events processed:      5
Book updates:          3
Trades:                2
Orders submitted:      2
Fills:                 3
Ending inventory:      5
Realized gross PnL:    0
Unrealized gross PnL:  2 x2
Fees:                  5
Ending equity:         -8 x2
Final book hash:       9ca1786003897355
Run hash:              8aca37583ca6f83a
```

The run writes deterministic artifacts:

- `run_manifest.json`
- `orders.csv`
- `fills.csv`
- `ledger.csv`
- `portfolio_summary.json`
- `metrics.json`

Without `--force`, the CLI refuses to overwrite an existing artifact directory that already contains run artifacts.

## Architecture

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

Python is a thin orchestration layer over the same authoritative C++ implementation:

```text
Python research/orchestration
           |
           v
       ReplayEngine
           |
           v
        C++ core
```

Detailed architecture notes are in `docs/architecture.md`.

## Build And Test

Debug:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Release:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

ASan/UBSan where supported:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

The project version is `0.1.0`, sourced from the CMake project version and exposed by the C++ and Python APIs.

## Python Binding

The optional binding builds a module named `market_replay`.

```bash
python3 -m pip install pybind11
cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON \
  -Dpybind11_DIR="$(python3 -c 'import pybind11; print(pybind11.get_cmake_dir())')"
cmake --build build-python
ctest --test-dir build-python --output-on-failure

PYTHONPATH=build-python python3 -c "import market_replay; assert market_replay.__version__ == '0.1.0'"
PYTHONPATH=build-python python3 examples/python_replay.py
```

Minimal Python usage:

```python
import market_replay

config = market_replay.ReplayConfig.from_file("configs/example_config.kv")
result = market_replay.ReplayEngine(config).run()

print(result.processed_event_count)
print(result.ending_inventory)
print(result.final_book_hash)
print(result.run_hash)
```

Canonical accounting, ticks, timestamps, quantities, fees, and hashes are exposed as exact Python `int` or `str` values.
Unavailable optional marks are exposed as `None`. Orders and fills are Python-owned read-only snapshots.

The binding does not provide Python strategy callbacks or Python-side replay/accounting/order-book implementations.

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

`price_format=ticks` treats `price` as integer `PriceTicks`. `price_format=decimal` converts decimal prices through the
documented exact tick conversion layer. Decimal prices that are not exact multiples of `tick_size` are rejected.

## Determinism And Correctness

Important invariants:

- Market events are ordered by `TimestampNs` and deterministic `sequence_id`.
- Historical market events precede same-timestamp internal events.
- Simulated execution does not mutate the authoritative historical `OrderBook`.
- Persistent hashes use canonical text and exclude local paths, output directories, timestamps, and usernames.
- Half-tick portfolio marks are stored exactly in doubled units.
- Arithmetic validates invalid values and checks overflow-sensitive accounting operations.
- Golden regression preserves final book hash `9ca1786003897355` and run hash `8aca37583ca6f83a`.

CTest covers unit, integration, golden, error-path, repeatability, sanitizer, benchmark smoke, and Python equivalence
checks. Details are in `docs/determinism.md` and the component docs under `docs/`.

## Performance

Release benchmarks use the repository-native deterministic harness:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/benchmark_replay --output-dir benchmarks/results --scales 100000,1000000 --repetitions 5 --warmups 1
```

Headline results are medians from five measured repetitions after one warmup on the same deterministic 1M-event
single-threaded workload. `ns/event` is replay processing cost, not exchange, order, or network latency.

| Scope | Events/sec | ns/event | Notes |
|---|---:|---:|---|
| Bare event iteration | 667,074,026.539 | 1.499 | No dispatch or book mutation |
| Variant dispatch only | 97,203,380.967 | 10.288 | Event type/payload access only |
| OrderBook apply | 46,414,838.935 | 21.545 | Direct book-update microbenchmark |
| Phase 11 core replay baseline | 24,639,240.859 | 40.586 | Frozen pre-optimization baseline |
| Phase 12A optimized core replay | 36,647,096.995 | 27.287 | Current documented hot replay baseline |

Benchmark scopes differ and values are not additive. The `OrderBook apply` row applies direct book updates, while core
replay includes mixed book/trade events and replay orchestration.

The optimization story:

- Phase 11 coarse decomposition showed significant replay orchestration/trace overhead beyond direct book mutation.
- Phase 12A changed trace collection to compact typed records and deferred canonical trace materialization.
- Hot replay throughput improved by 48.735%: 24.64M to 36.65M events/sec, a 1.487x improvement.
- Canonical trace semantics, final book hash, and run hash remained unchanged.
- Phase 13 measured 26.763 ns/event as a native regression check; it is documented as no material performance
  regression, not as an additional optimization claim.

Full methodology and CSV summaries are in `docs/benchmarks.md` and `benchmarks/results/`.

## Execution Model Boundary

Passive fills from L2 data use a queue-depth approximation and are not exact FIFO/L3 exchange reconstruction.

The simulator does not model:

- historical market impact from simulated orders;
- hidden liquidity or iceberg behavior;
- exchange-specific matching rules;
- maker/taker fee differences;
- margin, leverage, capital constraints, or risk limits;
- live trading or exchange connectivity.

See `docs/execution_model.md` for the exact passive-fill and latency rules.

## Documentation Index

- `docs/architecture.md`: component flow and Python/C++ boundary.
- `docs/domain_types.md`: integer units, price conversion, enum parsing, event keys.
- `docs/data_contract.md`: normalized CSV schema and parser policy.
- `docs/order_book.md`: L2 book update semantics, state hashes, locked/crossed policy.
- `docs/event_loop.md`: simulation clock, scheduler, event ordering, trace hashes.
- `docs/strategy.md`: strategy callback API and queue-imbalance demo.
- `docs/execution_model.md`: order lifecycle, latency, passive queue approximation.
- `docs/accounting.md`: FIFO portfolio accounting and mark-to-market policy.
- `docs/replay_engine.md`: config, artifacts, run manifest, hashing.
- `docs/determinism.md`: deterministic ordering, canonical state, regression coverage.
- `docs/benchmarks.md`: methodology, benchmark variants, measured results.

## Repository Structure

```text
include/      public C++ headers
src/          C++ implementation
apps/         replay_cli
strategies/   demo queue-imbalance strategy
python/       pybind11 binding
tests/        unit, integration, golden, and Python tests
benchmarks/   deterministic benchmark harness and public CSV summaries
docs/         detailed design notes
examples/     Python example
configs/      public synthetic replay config
artifacts/    local run outputs, ignored except .gitkeep
```

## Development Notes

- CI builds native C++, sanitizer C++, and optional Python binding jobs on Ubuntu.
- `PROJECT_SPEC.md` and local build/artifact outputs are intentionally ignored and are not required for public builds.
- `LICENSE` contains the MIT license.

## Limitations

- The queue model is an L2 approximation, not exact FIFO.
- The demo strategy is deterministic infrastructure coverage, not a profitability claim.
- Market impact, hidden liquidity, exchange-specific matching, leverage, margin, and risk limits are not modeled.
- Python bindings expose orchestration and result inspection only.
- The current core replay is single-threaded.
