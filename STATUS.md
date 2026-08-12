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
- CTest Debug: 18/18 passed
- CTest Release: 18/18 passed
- CTest ASan/UBSan: 18/18 passed
- Benchmark generator/decomposition self-test: PASS
- CLI help and golden CLI regression through CTest: PASS

## Benchmarks
Baseline and benchmark-only coarse replay decomposition established. See `docs/benchmarks.md` and
`benchmarks/results/`.

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
- Preserved the existing Phase 11 `core_preloaded_replay` benchmark as the headline full current core replay baseline.
- Added benchmark-only `bare_event_iteration` over the preloaded `MarketEvent` vector with no `MarketEvent` accessor
  calls, no dispatch, no trace, and no book mutation.
- Added benchmark-only `event_type_dispatch_only` over the same preloaded `MarketEvent` vector with existing
  `MarketEvent::type()` dispatch and typed payload reads, but no `OrderBook` mutation and no `EventLoop` trace.
- Retained the existing direct `order_book_apply` microbenchmark as the reference for `OrderBook::apply()` mutation cost.
- Did not add a trace-disabled `EventLoop + OrderBook` variant because trace collection could not be bypassed through an
  existing benchmark/test-only configuration without changing normal production `EventLoop` behavior.
- Added deterministic guard checksum verification for non-mutating decomposition variants.
- Regenerated public benchmark CSV summary and repetition files.
- Updated `docs/benchmarks.md` with the coarse timing decomposition, comparability notes, skipped optional trace variant,
  and exact Release medians.
- Did not optimize production code, replace containers, change canonical production hashing, change event ordering, add
  multithreading, or change replay semantics.

## Files changed
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
- Functional hashes are verified after timing where state mutation occurs.
- Non-mutating decomposition variants use deterministic guard checksums to prevent dead-code elimination and verify repeat
  stability; those checksums are not canonical production hashes.
- Warmup repetitions are not reported.
- Measured repetitions are written to CSV.
- Median, min, and max batch ns/event are written to summary CSV.

## Benchmark environment
- Date: 2026-08-12.
- Git commit: `fbfb48f`.
- Working tree clean at benchmark time: false.
- OS: macOS 26.5.1, Darwin 25.5.0, arm64.
- Hardware: MacBook Air Mac14,2, Apple M2, 8 cores, 8 GB memory.
- Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`).
- Build type: Release.
- Release flags: `-O3 -DNDEBUG`.
- `sysctl` access was restricted in the sandbox; hardware details were captured with `system_profiler` during the
  original Phase 11 baseline and private serial/UUID fields were not recorded publicly.

## Synthetic workload
- Bid and ask sides alternate for generated book updates.
- Prices span 256 levels per side.
- Quantities are deterministic replacements.
- Every 23rd book update deletes a level.
- Core replay uses preloaded typed `MarketEvent` objects, with every fifth event represented as a trade.
- The bare iteration, dispatch-only, and full core replay variants use the same deterministic mixed 1M `MarketEvent`
  vector.
- The direct `order_book_apply` reference applies 1M generated `BookUpdateEvent` objects and is not directly comparable
  event-for-event to the 1M mixed replay workload.
- No random device, private data, or large generated dataset files are used.

## Coarse 1M replay decomposition
- `bare_event_iteration`: 633,395,659.466 events/sec, 1.579 ns/event, guard checksum `b43a3a668619fe0e`.
- `event_type_dispatch_only`: 88,443,722.154 events/sec, 11.307 ns/event, guard checksum `44a3decf7d24b334`.
- `order_book_apply`: 45,301,053.132 events/sec, 22.075 ns/event, final book hash `a7145c79f43da48a`.
- `core_preloaded_replay`: 24,639,240.859 events/sec, 40.586 ns/event, final book hash `2855ca09d92eee1f`.
- Trace-disabled `EventLoop + OrderBook`: not run; no safe benchmark/test-only bypass exists without changing normal
  production behavior.

## 100K benchmark medians
- `order_book_apply`: 41,660,886.219 events/sec, 24.003 ns/event, final book hash `e3c154d22706031c`.
- `bare_event_iteration`: 637,958,532.695 events/sec, 1.567 ns/event, guard checksum `a4445da1d4c19ace`.
- `event_type_dispatch_only`: 94,343,271.768 events/sec, 10.600 ns/event, guard checksum `55d684c8b390e7f1`.
- `core_preloaded_replay`: 26,714,453.481 events/sec, 37.433 ns/event, final book hash `7881be7964fe68e6`.

## End-to-end result
- Public Phase 10 example: 3,920.157 events/sec, 255,091.800 ns/event over 5 events.
- Run hash: `8aca37583ca6f83a`.
- This includes config/input loading, strategy/execution/accounting, and artifact writing, and is not compared directly
  with preloaded replay throughput.

## Functional hashes and checksums
- All `order_book_apply` repetitions produced identical final book hashes per scale.
- All `core_preloaded_replay` repetitions produced identical final book hashes per scale.
- All non-mutating iteration/dispatch repetitions produced identical guard checksums per scale.
- End-to-end benchmark repetitions produced the Phase 10 golden run hash `8aca37583ca6f83a`.

## Coarse timing interpretation
- Dispatch and typed payload access add about 9.7 ns/event over bare iteration on the same 1M mixed `MarketEvent` vector.
- Full current core replay adds about 29.3 ns/event over dispatch-only on the same 1M mixed `MarketEvent` vector.
- That remaining cost includes copying the preloaded vector into `EventLoop`, EventLoop loop/control flow, trace entry
  construction and storage, and `OrderBook` mutation for the 800K book-update events.
- The direct `order_book_apply` row remains the isolated book-mutation reference, but it applies 1M book updates rather
  than the mixed core replay workload's 800K book updates and 200K trades.
- These are coarse timing observations, not CPU-sampled percentages.

## Candidate Phase 12 optimization targets
- Investigate `OrderBook` representation and `std::map` insert/update/delete cost.
- Measure whether trace recording should be configurable for benchmark/replay modes while preserving deterministic test
  traces.
- Investigate allocation/copy overhead around `EventLoop` setup for preloaded replay.
- Separate parser/artifact I/O measurements before optimizing application-level runtime.

## Acceptance gate results
- benchmark-only decomposition variants added: PASS
- existing full core replay benchmark preserved as headline baseline: PASS
- Release build used for performance numbers: PASS
- 1 warmup and 5 measured repetitions: PASS
- median ns/event and events/sec reported: PASS
- same deterministic 1M mixed workload used where comparisons are valid: PASS
- final functional hashes verified where state mutation occurs: PASS
- non-mutating guard checksums verified for repeat stability: PASS
- trace-disabled optional variant skipped unless safely isolated: PASS
- no production optimization, container replacement, hashing change, event-ordering change, replay semantic change, or
  multithreading added: PASS
- Debug, Release, and ASan/UBSan builds/tests pass: PASS
- `docs/benchmarks.md` and `STATUS.md` updated: PASS
- Phase 12 not started: PASS

## Privacy/Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored by `.gitignore`.
- Generated artifact directories under `artifacts/` remain ignored.
- Generated benchmark end-to-end artifact subdirectories under `benchmarks/results/e2e_repetition_*/` are ignored.
- Public benchmark results are small CSV summaries only.
- No private benchmark datasets were added.
- No absolute local-machine paths are intended in public files.
- No username, serial number, hardware UUID, private dataset path, or private machine file location is recorded in public
  benchmark docs/artifacts.
- No staged changes.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- L2 data cannot identify exact FIFO position, exact queue composition, hidden or iceberg liquidity, or cancellations
  ahead of our simulated order.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Accounting is single-instrument and consumes existing `Fill` records only.
- Benchmark results were captured from a dirty working tree because Phase 11 harness/docs/results were uncommitted.
- OS-level sampling profiler output and memory statistics were unavailable under sandbox restrictions.
- 10M scale was not run.
- Trace-disabled replay timing was not run because trace collection could not be isolated safely without changing normal
  `EventLoop` behavior.
- Non-mutating decomposition checksums are benchmark guard checksums, not canonical production hashes.
- Phase 11 does not claim any speedup or optimization.
- Portfolio does not enforce margin, leverage, capital constraints, risk limits, or multi-asset allocation.
- Strategy performance statistics such as Sharpe, Sortino, volatility, alpha, beta, drawdown, win rate, and return series
  are not implemented.
- Market impact, Python bindings, multithreading, and performance optimization are not implemented.
- Phase 12 optimization has not been started.

## Next phase
Phase 12 - Profile-Guided Optimization

## Verification commands
```bash
$ sed -n '1,220p' PROJECT_SPEC.md
Result: PASS - spec read started.

$ sed -n '221,520p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '521,900p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '901,1300p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1301,1700p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1701,2200p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '2201,2600p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '2601,3000p' PROJECT_SPEC.md
Result: PASS - spec read completed.

$ sed -n '1,240p' STATUS.md
Result: PASS - current phase and Phase 11 PASS status inspected before changes.

$ git status --short
Result: PASS - existing Phase 11 working-tree changes observed; no staged changes.

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built Debug targets including `benchmark_replay`.

$ ctest --test-dir build --output-on-failure
Result: PASS - 18/18 tests passed.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets including `benchmark_replay`.

$ ./build-release/benchmark_replay --output-dir benchmarks/results --scales 100000,1000000 --repetitions 5 --warmups 1
Result: PASS - wrote `baseline_summary.csv` and `baseline_repetitions.csv`; all hashes/checksums stable.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - 18/18 tests passed.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets including `benchmark_replay`.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 18/18 tests passed under ASan/UBSan configuration.

$ cmake --build build
Result: PASS - final Debug rebuild after benchmark harness formatting update.

$ cmake --build build-release
Result: PASS - final Release rebuild after benchmark harness formatting update.

$ cmake --build build-asan
Result: PASS - final ASan/UBSan rebuild after benchmark harness formatting update.

$ ctest --test-dir build --output-on-failure
Result: PASS - final Debug CTest run passed 18/18 tests.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - final Release CTest run passed 18/18 tests.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - final ASan/UBSan CTest run passed 18/18 tests.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!artifacts/**' -g '!benchmarks/results/e2e_repetition_*/**' -g '!.git/**'
Result: PASS - no absolute local-machine paths found in public files.

$ git status --short
Result: PASS - only benchmark decomposition files are modified; no staged changes.

$ git status --ignored --short
Result: PASS - private spec, generated artifacts, benchmark E2E artifact directories, and build directories are ignored.
```
