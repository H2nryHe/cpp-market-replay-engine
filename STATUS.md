# Project Status

## Current phase
Phase 10 - End-to-End Replay Engine, CLI & Artifacts

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 17/17 passed
- CLI help: PASS
- Public synthetic CLI example: PASS
- Golden end-to-end replay: PASS
- Determinism: PASS for 100 repeated end-to-end runs

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
- Phase 9 - PASS
- Phase 10 - PASS

## Implementation summary
- Added `ReplayEngine`, an application-level orchestrator for normalized feed -> event loop -> historical book -> strategy -> latency-aware execution -> fills -> portfolio -> artifacts.
- Replaced the stub CLI with `replay_cli --config <path> [--output <directory>] [--force]`.
- Added deterministic key=value replay config parsing and validation.
- Added deterministic CSV/JSON artifact generation: `orders.csv`, `fills.csv`, `ledger.csv`, `portfolio_summary.json`, `metrics.json`, and `run_manifest.json`.
- Added content-based input hashes, canonical config hash, artifact hashes, final book hash, and run hash.
- Added safe output writing through a temporary directory and explicit overwrite refusal unless `--force` is supplied.
- Added a public synthetic end-to-end fixture and exact golden expected artifacts.
- Added integration and CLI CTest coverage for valid runs, golden outputs, exact counts, determinism, path-independent hashes, config sensitivity, missing/malformed input, invalid config, invalid output, overwrite refusal, empty feed, mark-unavailable behavior, conservation, and historical book immutability.
- Updated README and added `docs/replay_engine.md`.

## Files changed
- `CMakeLists.txt`
- `README.md`
- `apps/replay_cli.cpp`
- `include/replay/replay_engine.hpp`
- `src/replay_engine.cpp`
- `configs/example_config.kv`
- `docs/replay_engine.md`
- `tests/integration/replay_engine_test.cpp`
- `tests/golden/e2e_book_updates.csv`
- `tests/golden/e2e_trades.csv`
- `tests/golden/e2e_empty_book_updates.csv`
- `tests/golden/e2e_empty_trades.csv`
- `tests/golden/e2e_one_sided_book_updates.csv`
- `tests/golden/e2e_one_sided_trades.csv`
- `tests/golden/e2e_invalid_config.kv`
- `tests/golden/e2e_missing_input_config.kv`
- `tests/golden/e2e_malformed_book_updates.csv`
- `tests/golden/expected_orders.csv`
- `tests/golden/expected_fills.csv`
- `tests/golden/expected_ledger.csv`
- `tests/golden/expected_portfolio_summary.json`
- `tests/golden/expected_metrics.json`
- `tests/golden/expected_run_manifest.json`
- `STATUS.md`

## ReplayEngine/orchestration design
- `ReplayEngine` composes existing components and keeps separation of concerns.
- Market data are parsed as normalized book-update and trade feeds, then merged by deterministic `EventKey`.
- The Phase 4 `EventLoop` applies book updates to the historical `OrderBook`, dispatches strategy callbacks, processes scheduled order arrivals/cancels, and drains pending internal events after historical feed exhaustion.
- `LatencyAwareExecution` owns order lifecycle and fill generation.
- `Portfolio` applies only newly generated `Fill` records.
- Simulated orders, execution, passive fills, and cancels do not mutate the historical `OrderBook`.

## Config format
- Format: deterministic line-oriented `key=value`.
- Blank lines and `#` comments are ignored.
- Public example: `configs/example_config.kv`.
- Required operational fields include book/trade input paths, output directory, price format, strategy type, latency, queue fraction, fee rate, and initial cash.
- `strategy_type=queue_imbalance` uses the existing Queue Imbalance strategy.
- `strategy_type=scripted` is available for deterministic golden/integration scenarios without altering the QI demo.
- Canonical config serialization excludes input file paths and output paths; input content is identified by content hash.

## Artifact formats
- `orders.csv`: deterministic order records sorted by `order_id`.
- `fills.csv`: deterministic fill records in fill-sequence order.
- `ledger.csv`: deterministic portfolio ledger after each fill.
- `portfolio_summary.json`: fixed-order accounting summary, doubled-unit mark values, and mark availability.
- `metrics.json`: fixed-order operational/execution counts only.
- `run_manifest.json`: fixed-order engine version, input/config/artifact hashes, counts, final accounting values, final book hash, and run hash.

## Output overwrite policy
- Successful runs write into a temporary output directory first.
- Required artifacts are written before the final output directory is renamed into place.
- Existing output directories containing run artifacts are refused unless `--force` is supplied.
- Invalid output paths fail cleanly and do not produce a success manifest.

## Canonical hashing design
- Hash algorithm: deterministic FNV-1a over explicit canonical text.
- No `std::hash` is used for persistent run/artifact hashes.
- Input hashes are file content hashes.
- Canonical config hash excludes absolute path spelling and output directory.
- Run hash is derived from engine version, input hash, config hash, final book hash, orders hash, fills hash, portfolio hash, and metrics hash.
- Wall-clock generation timestamps are omitted.
- Golden run hash: `8aca37583ca6f83a`.

## Final mark behavior
- Final mark uses Phase 9 policy.
- Valid two-sided non-crossed final book: mark available.
- Locked final book: mark available.
- Empty, one-sided, or crossed final book with nonzero inventory: mark unavailable.
- Flat portfolio with no midpoint still has cash-only equity.
- Half-tick values are preserved in doubled accounting units.

## Golden E2E results
- Historical events processed: 5
- Book updates: 3
- Trades: 2
- Internal events: 3
- Strategy intents: 2
- Orders submitted: 2
- Fills: 3
- Ending inventory: 5
- Ending cash: -50009
- Realized gross PnL: 0
- Unrealized gross PnL x2: 2
- Total fees: 5
- Ending equity x2: -8
- Final book hash: `9ca1786003897355`
- Run hash: `8aca37583ca6f83a`

## Determinism results
- The integration test repeats identical config/input/output-overridden runs 100 times.
- Repeated runs produce identical input hash, config hash, orders hash, fills hash, portfolio hash, final book hash, and run hash.
- Running identical input/config into two different output directories preserves the same run hash.
- Copying identical input content to different paths preserves input hashes and run hash when other canonical config semantics are unchanged.
- Changing only latency changes config hash and run hash while preserving input hash.
- Changing queue fraction changes config hash and run hash.

## Privacy/Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored by `.gitignore`.
- Generated artifact directories under `artifacts/` are ignored.
- No private/local-only files are staged.
- Public configs, fixtures, docs, and golden artifacts contain synthetic data only.
- No absolute local-machine paths were found in public files.
- Run manifests do not include absolute input paths, output paths, local usernames, secrets, build directories, or wall-clock timestamps.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- L2 data cannot identify exact FIFO position, exact queue composition, hidden or iceberg liquidity, or cancellations ahead of our simulated order.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Accounting is single-instrument and consumes existing `Fill` records only.
- The manifest records Git commit as `unavailable`; build-time Git metadata embedding is not implemented.
- Strategy cancel intents are not a general public strategy API; Phase 10 uses an orchestrator cancel-after-arrival setting for the synthetic integration scenario.
- Metrics are operational counts only; benchmark throughput and latency metrics are not implemented.
- Portfolio does not enforce margin, leverage, capital constraints, risk limits, or multi-asset allocation.
- Strategy performance statistics such as Sharpe, Sortino, volatility, alpha, beta, drawdown, win rate, and return series are not implemented.
- Market impact, benchmarking, Python bindings, multithreading, and performance optimization are not implemented.
- Phase 11 benchmark baseline has not been started.

## Next phase
Phase 11 - Benchmark Baseline & Profiling

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, execution_tests, latency_execution_tests, passive_limit_tests, portfolio_tests, and replay_engine_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 17/17 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage for --config, --output, --force, and --help.

$ ./build/replay_cli --config configs/example_config.kv --output artifacts/example_run --force
Result: PASS - exited 0, wrote required artifacts, final book hash `9ca1786003897355`, run hash `8aca37583ca6f83a`.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 17/17 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build-release/replay_cli --config configs/example_config.kv --output artifacts/example_run_release --force
Result: PASS - exited 0, final book hash `9ca1786003897355`, run hash `8aca37583ca6f83a`.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!artifacts/**' -g '!.git/**'
Result: PASS - no absolute local-machine paths found in public files.

$ rg "events/sec|ns/event|p99|Sharpe|drawdown|alpha|win rate|annual return|HFT-grade|production exchange engine|exact FIFO|exact L3|profitable strategy|realistic fills" README.md docs include src apps tests configs CMakeLists.txt
Result: PASS - matches are limitation/non-goal language only.

$ git check-ignore -v PROJECT_SPEC.md artifacts/example_run/run_manifest.json artifacts/example_run_release/run_manifest.json
Result: PASS - `PROJECT_SPEC.md` and generated artifact outputs are ignored.

$ git status --short
Result: PASS - only Phase 10 files are modified/untracked; no staged changes.

$ git status --ignored --short
Result: PASS - `PROJECT_SPEC.md`, generated artifacts, and build directories are ignored.
```

## Phase 10 acceptance gate
- complete CLI run succeeds: PASS
- golden E2E test passes: PASS
- deterministic artifacts produced: PASS
- manifest contains required metadata: PASS
- failure modes are clear: PASS
- output overwrite policy tested: PASS
- run hash repeatability tested: PASS
- output path independence tested: PASS
- input content hash independence from source path tested: PASS
- final mark available/unavailable behavior tested: PASS
- order/fill conservation tested: PASS
- historical book immutability tested: PASS
- all Phase 0-9 tests continue to pass: PASS
- Phase 11 not started: PASS
