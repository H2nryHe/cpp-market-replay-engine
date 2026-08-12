# Replay Engine And Artifacts

Phase 10 introduces `ReplayEngine`, an application-level coordinator for deterministic end-to-end runs.

## Orchestration

`ReplayEngine` composes:

- normalized book-update and trade feeds;
- the deterministic `EventLoop` and `SimulationClock`;
- the historical `OrderBook`;
- a configured `Strategy`;
- `LatencyAwareExecution`;
- `Portfolio`;
- deterministic artifact serialization.

The historical book remains authoritative market state. Simulated orders, fills, and cancels do not mutate historical
book levels.

## Config

The config format is line-oriented `key=value` text. Blank lines and lines beginning with `#` are ignored. Required
paths identify input files and output directory, but canonical config hashing excludes file paths and output paths.
Input data are identified by content hashes instead.

Required example:

```text
book_updates_path=tests/golden/e2e_book_updates.csv
trades_path=tests/golden/e2e_trades.csv
output_directory=artifacts/example_run
price_format=ticks
strategy_type=scripted
order_latency_ns=50
queue_fraction_ppm=500000
fee_rate_ppm=100
initial_cash=0
```

## Output Policy

Artifacts are written to a temporary directory first. The final output directory is renamed into place only after all
required artifacts are written. Existing run artifacts are not overwritten unless `--force` is supplied.

## Canonical Artifacts

All artifact field ordering is fixed in code:

- `orders.csv`
- `fills.csv`
- `ledger.csv`
- `portfolio_summary.json`
- `metrics.json`
- `run_manifest.json`

The manifest intentionally omits generated-at timestamps and absolute local paths.

## Hashes

Hashes are deterministic FNV-1a over canonical text. The run hash is independent of output directory and input file path
spelling. It changes when canonical config fields or functional output artifacts change.
