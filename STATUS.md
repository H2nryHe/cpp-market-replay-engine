# Project Status

## Current phase
Phase 11 - Benchmark Baseline & Profiling

## Phase status
PASS

## Last verified commit
fbfb48f

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 18/18 passed
- CLI help: PASS
- Phase 10 golden CLI regression: PASS
- Benchmark generator determinism self-test: PASS

## Benchmarks
Baseline established. See `docs/benchmarks.md` and `benchmarks/results/`.

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
- Phase 11 - PASS

## Implementation summary
- Added repository-native `benchmark_replay` harness.
- Added deterministic synthetic in-memory benchmark generator.
- Added Release-only baseline measurements for:
  - `OrderBook::apply()` update throughput.
  - Core preloaded `EventLoop` + historical `OrderBook` replay throughput.
  - Tiny public end-to-end `ReplayEngine` example runtime, labeled separately.
- Added benchmark self-test to CTest for generator determinism and functional hashes.
- Added small public benchmark CSV outputs:
  - `benchmarks/results/baseline_summary.csv`
  - `benchmarks/results/baseline_repetitions.csv`
- Added `docs/benchmarks.md` with methodology, environment, baseline results, variability, profiling method, measured hotspots, Phase 12 candidates, and limitations.
- Updated README with benchmark command and docs pointer.
- Did not optimize production hot paths or change replay/order-book semantics.

## Files changed
- `.gitignore`
- `CMakeLists.txt`
- `README.md`
- `benchmarks/benchmark_replay.cpp`
- `benchmarks/results/baseline_summary.csv`
- `benchmarks/results/baseline_repetitions.csv`
- `docs/benchmarks.md`
- `STATUS.md`

## Benchmark harness design
- Native C++ harness using `std::chrono::steady_clock`.
- No Google Benchmark dependency added.
- Workloads are generated deterministically in memory.
- Headline timed regions exclude data generation for order-book and core replay benchmarks.
- Functional hashes are verified after timed runs.
- Warmup repetitions are not reported.
- Measured repetitions are written to CSV.
- Median, min, and max batch ns/event are written to summary CSV.

## Synthetic workload
- Bid and ask sides alternate.
- Prices span 256 levels per side.
- Quantities are deterministic replacements.
- Every 23rd book update deletes a level.
- Core replay uses preloaded typed `MarketEvent` objects, with every fifth event represented as a trade.
- No random device, private data, or large generated dataset files are used.

## Event scales
- 100K order-book updates: measured.
- 1M order-book updates: measured.
- 100K core preloaded replay events: measured.
- 1M core preloaded replay events: measured.
- 10M scale: not run in Phase 11; documented as not run rather than inventing data.
- End-to-end public example: measured separately over 5 events.

## Repetition/warmup policy
- Warmups: 1 untimed run per benchmark/scale.
- Measured repetitions: 5.
- Headline value: median measured batch run.
- Repetition CSV preserves all measured repetitions.

## Benchmark environment
- Date: 2026-08-12.
- Git commit: `fbfb48f`.
- Working tree clean at benchmark time: false.
- OS: macOS 26.5.1, Darwin 25.5.0, arm64.
- Hardware: MacBook Air Mac14,2, Apple M2, 8 cores, 8 GB memory.
- Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`).
- Build type: Release.
- Release flags: `-O3 -DNDEBUG`.
- `sysctl` access was restricted in the sandbox; hardware details were captured with `system_profiler` and private serial/UUID fields were not recorded publicly.

## Median OrderBook throughput
- 100K: 43,403,568.989 events/sec, 23.040 ns/event, hash `e3c154d22706031c`.
- 1M: 46,764,482.376 events/sec, 21.384 ns/event, hash `a7145c79f43da48a`.

## Median core replay throughput
- 100K: 25,711,349.478 events/sec, 38.893 ns/event, hash `7881be7964fe68e6`.
- 1M: 26,226,068.712 events/sec, 38.130 ns/event, hash `2855ca09d92eee1f`.

## End-to-end result
- Public Phase 10 example: 6,366.045 events/sec, 157,083.400 ns/event over 5 events.
- Run hash: `8aca37583ca6f83a`.
- This includes config/input loading, strategy/execution/accounting, and artifact writing, and is not compared directly with preloaded replay throughput.

## Functional hashes
- All order-book benchmark repetitions produced identical final book hashes per scale.
- All core replay repetitions produced identical final book hashes per scale.
- End-to-end benchmark repetitions produced the Phase 10 golden run hash `8aca37583ca6f83a`.
- Release CLI golden regression preserved final book hash `9ca1786003897355` and run hash `8aca37583ca6f83a`.

## Profiling method
- Attempted macOS `sample` on a longer 1M-event Release benchmark process.
- `sample` could not inspect the process under sandbox restrictions without elevated permissions.
- `/usr/bin/time -l` returned real/user/sys timing but could not capture extended memory data because `sysctl` access was restricted.
- Fallback profiling evidence uses coarse component timing from the Release harness.

## Measured hotspots
- `OrderBook::apply()` and ordered-map mutation path are the largest measured component:
  - 1M direct book mutation median: 21.384 ns/event.
  - 1M core replay median: 38.130 ns/event.
  - Direct book mutation is roughly 56% of measured core replay batch time.
- EventLoop construction/dispatch, trace recording, market-event variant access, and loop control account for the remaining measured core replay overhead.
- End-to-end runtime for the tiny public example includes I/O and artifact writing, so it is not a core replay bottleneck measurement.

## Candidate Phase 12 optimization targets
- Investigate `OrderBook` representation and `std::map` insert/update/delete cost.
- Measure whether trace recording should be configurable for benchmark/replay modes while preserving deterministic test traces.
- Investigate allocation/copy overhead around `EventLoop` setup for preloaded replay.
- Separate parser/artifact I/O measurements before optimizing application-level runtime.

## Privacy/Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored by `.gitignore`.
- Generated artifact directories under `artifacts/` remain ignored.
- Generated benchmark end-to-end artifact subdirectories under `benchmarks/results/e2e_repetition_*/` are ignored.
- Public benchmark results are small CSV summaries only.
- No private benchmark datasets were added.
- No absolute local-machine paths were found in public files.
- No username, serial number, hardware UUID, private dataset path, or private machine file location is recorded in public benchmark docs/artifacts.
- No staged changes.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- L2 data cannot identify exact FIFO position, exact queue composition, hidden or iceberg liquidity, or cancellations ahead of our simulated order.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Accounting is single-instrument and consumes existing `Fill` records only.
- Benchmark results were captured from a dirty working tree because Phase 11 harness/docs/results were uncommitted.
- OS-level sampling profiler output and memory statistics were unavailable under sandbox restrictions.
- 10M scale was not run.
- Phase 11 does not claim any speedup or optimization.
- Portfolio does not enforce margin, leverage, capital constraints, risk limits, or multi-asset allocation.
- Strategy performance statistics such as Sharpe, Sortino, volatility, alpha, beta, drawdown, win rate, and return series are not implemented.
- Market impact, Python bindings, multithreading, and performance optimization are not implemented.
- Phase 12 optimization has not been started.

## Next phase
Phase 12 - Profile-Guided Optimization

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, benchmark_replay, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, execution_tests, latency_execution_tests, passive_limit_tests, portfolio_tests, and replay_engine_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 18/18 tests passed.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets including benchmark_replay.

$ ./build-release/benchmark_replay --output-dir benchmarks/results --scales 100000,1000000 --repetitions 5 --warmups 1
Result: PASS - wrote baseline_summary.csv and baseline_repetitions.csv; all functional hashes stable.

$ sample <benchmark_pid> 3 -file /tmp/cpp_market_replay_profile/sample.txt
Result: FAIL/UNAVAILABLE - sandbox denied process inspection without elevated permissions; no CPU-sample percentages reported.

$ /usr/bin/time -l ./build-release/benchmark_replay --output-dir /tmp/cpp_market_replay_time --scales 1000000 --repetitions 5 --warmups 1 --no-e2e
Result: PARTIAL - command ran benchmark successfully and reported real/user/sys timing; extended memory statistics unavailable because sandbox blocked `sysctl`.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 18/18 tests passed under ASan/UBSan configuration.

$ ./build-release/replay_cli --config configs/example_config.kv --output artifacts/example_run_release --force
Result: PASS - final book hash `9ca1786003897355`, run hash `8aca37583ca6f83a`.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!artifacts/**' -g '!benchmarks/results/e2e_repetition_*/**' -g '!.git/**'
Result: PASS - no absolute local-machine paths found in public files.

$ rg "HFT-grade|ultra-low-latency|institutional-speed|nanosecond trading engine|production exchange engine|exact FIFO|exact L3|profitable strategy|realistic fills" README.md docs benchmarks CMakeLists.txt
Result: PASS - matches are limitation/non-goal language only.

$ git check-ignore -v PROJECT_SPEC.md artifacts/example_run_release/run_manifest.json benchmarks/results/e2e_repetition_0/run_manifest.json
Result: PASS - private spec and generated artifact directories are ignored.

$ git status --short
Result: PASS - only Phase 11 files are modified/untracked; no staged changes.

$ git status --ignored --short
Result: PASS - `PROJECT_SPEC.md`, generated artifacts, benchmark E2E artifact directories, and build directories are ignored.
```

## Phase 11 acceptance gate
- baseline benchmark exists: PASS
- methodology documented: PASS
- 5 measured repetitions collected: PASS
- median reported: PASS
- functional checksum verified for every run: PASS
- Release build used for performance measurements: PASS
- Debug/ASan regression tests pass: PASS
- Phase 10 golden hashes preserved: PASS
- profiling attempted and fallback evidence documented: PASS
- no optimization implemented: PASS
- Phase 12 not started: PASS
