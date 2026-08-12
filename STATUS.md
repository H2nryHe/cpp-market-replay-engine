# Project Status

## Current phase
Phase 12A - Trace Hot-Path / Replay Observability Refactor

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
- Canonical trace exact-byte fixture: PASS
- Trace hash equality fixture: PASS
- Same-timestamp market-before-internal ordering: PASS
- Same-timestamp internal insertion ordering: PASS
- Strategy callback order regression: PASS
- Latency zero/delayed-arrival regression: PASS
- Cancel race regression: PASS
- Phase 10 golden final book hash: PASS, `9ca1786003897355`
- Phase 10 golden run hash: PASS, `8aca37583ca6f83a`
- ReplayEngine 100-run determinism regression: PASS through CTest

## Benchmarks
Phase 12A retained. Optimized 1M hot replay median: 36,647,096.995 events/sec, 27.287 ns/event.
Frozen Phase 11 baseline: 24,639,240.859 events/sec, 40.586 ns/event.

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
- Phase 12A - PASS

## Phase 12A hypothesis
- Phase 11 showed substantial full replay overhead beyond direct `OrderBook::apply()` timing.
- Trace-disabled replay could not be isolated safely without changing normal EventLoop behavior.
- The targeted hypothesis was that the trace representation and trace-vector growth added avoidable hot-loop overhead.
- This was based on coarse benchmark decomposition, not sampling-profiler attribution.

## Implementation details
- Replaced optional-heavy trace entries with compact typed trace records:
  - event class,
  - trace kind,
  - timestamp,
  - deterministic sequence id,
  - existing label for internal events only.
- Reserved trace vector capacity for the known historical event count before replay.
- Preserved `EventLoopResult::trace`, `canonical_trace()`, `trace_hash()`, and `canonical_trace_line()` behavior.
- Added an exact canonical trace fixture with expected bytes and expected FNV-1a trace hash `0e1151708630d39a`.
- Added Phase 12A benchmark variants:
  - `core_preloaded_replay` as optimized hot replay with compact trace collection.
  - `core_replay_with_trace_materialization`.
  - `trace_materialization_only`.
- Kept `bare_event_iteration`, `event_type_dispatch_only`, and `order_book_apply` as reference measurements.
- Did not modify `OrderBook` containers, `std::map` choice, `MarketEvent` representation, `std::variant` dispatch,
  price/quantity types, parser behavior, event ordering, execution semantics, canonical production hashing, allocators,
  SIMD, or threading.

## Files changed
- `include/replay/event_loop.hpp`
- `src/event_loop.cpp`
- `tests/unit/event_loop_test.cpp`
- `benchmarks/benchmark_replay.cpp`
- `benchmarks/results/baseline_summary.csv`
- `benchmarks/results/baseline_repetitions.csv`
- `docs/benchmarks.md`
- `STATUS.md`

## 1M Phase 12A benchmark medians
- `bare_event_iteration`: 667,074,026.539 events/sec, 1.499 ns/event, guard checksum `b43a3a668619fe0e`.
- `event_type_dispatch_only`: 97,203,380.967 events/sec, 10.288 ns/event, guard checksum `44a3decf7d24b334`.
- `order_book_apply`: 46,414,838.935 events/sec, 21.545 ns/event, final book hash `a7145c79f43da48a`.
- `core_preloaded_replay`: 36,647,096.995 events/sec, 27.287 ns/event, final book hash `2855ca09d92eee1f`.
- `trace_materialization_only`: 3,798,326.574 events/sec, 263.274 ns/event, trace checksum `7b159670a635cca7`.
- `core_replay_with_trace_materialization`: 3,363,864.146 events/sec, 297.277 ns/event, combined hash
  `2855ca09d92eee1f:7b159670a635cca7`.

## 100K Phase 12A benchmark medians
- `order_book_apply`: 27,605,557.440 events/sec, 36.225 ns/event, final book hash `e3c154d22706031c`.
- `bare_event_iteration`: 636,602,072.776 events/sec, 1.571 ns/event, guard checksum `a4445da1d4c19ace`.
- `event_type_dispatch_only`: 88,580,549.837 events/sec, 11.289 ns/event, guard checksum `55d684c8b390e7f1`.
- `core_preloaded_replay`: 34,980,323.568 events/sec, 28.587 ns/event, final book hash `7881be7964fe68e6`.
- `trace_materialization_only`: 3,934,039.223 events/sec, 254.192 ns/event, trace checksum `e1a948341b11f657`.
- `core_replay_with_trace_materialization`: 3,531,530.725 events/sec, 283.163 ns/event, combined hash
  `7881be7964fe68e6:e1a948341b11f657`.

## Before/after comparison
- Frozen 1M Phase 11 hot replay baseline: 24,639,240.859 events/sec, 40.586 ns/event.
- Phase 12A optimized 1M hot replay: 36,647,096.995 events/sec, 27.287 ns/event.
- Absolute reduction: 13.299 ns/event.
- Relative throughput improvement: 1.487x, or 48.735%.
- Relative ns/event reduction: 32.767%.
- Phase 12A hot replay range: 26.078 to 27.922 ns/event.
- Frozen Phase 11 hot replay range: 39.255 to 41.239 ns/event.
- The refactor was retained because the median improvement is outside the recorded run-to-run ranges and all semantic
  checks passed.

## Replay plus trace materialization
- `core_preloaded_replay` measures replay processing with compact trace collection but without canonical text
  materialization.
- `core_replay_with_trace_materialization` measures replay plus generation of the complete canonical trace string.
- `trace_materialization_only` measures canonical trace generation from completed trace records.
- These scopes are not interchangeable and are documented separately in `docs/benchmarks.md`.

## Functional equivalence
- Canonical trace fixture bytes unchanged: PASS.
- Representative trace hash unchanged: PASS, `0e1151708630d39a`.
- 1M hot replay final book hash unchanged: PASS, `2855ca09d92eee1f`.
- Phase 10 final book hash unchanged: PASS, `9ca1786003897355`.
- Phase 10 run hash unchanged: PASS, `8aca37583ca6f83a`.
- Orders, fills, portfolio, artifact hashes, callback ordering, latency semantics, and cancel-race semantics preserved by
  existing CTest regressions.

## Benchmark environment
- Date: 2026-08-12.
- Git commit: `fbfb48f`.
- Working tree clean at benchmark time: false.
- OS: macOS 26.5.1, Darwin 25.5.0, arm64.
- Hardware: MacBook Air Mac14,2, Apple M2, 8 cores, 8 GB memory.
- Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`).
- Build type: Release.
- Release flags: `-O3 -DNDEBUG`.

## Privacy/Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored by `.gitignore`.
- Generated artifact directories under `artifacts/` remain ignored.
- Generated benchmark end-to-end artifact subdirectories under `benchmarks/results/e2e_repetition_*/` are ignored.
- Public benchmark results are small CSV summaries only.
- No private benchmark datasets were added.
- No absolute local-machine paths are intended in public files.
- No staged changes.

## Known limitations
- Results were collected from a dirty working tree because Phase 11 and Phase 12A harness/docs/results are uncommitted.
- OS-level sampling profiler output and memory statistics were unavailable under sandbox restrictions.
- 10M scale was not run.
- Canonical trace materialization remains expensive when explicitly requested.
- Non-mutating decomposition checksums are benchmark guard checksums, not canonical production hashes.
- Phase 12A does not optimize `OrderBook` containers or `MarketEvent` dispatch.
- Portfolio does not enforce margin, leverage, capital constraints, risk limits, or multi-asset allocation.
- Strategy performance statistics such as Sharpe, Sortino, volatility, alpha, beta, drawdown, win rate, and return series
  are not implemented.
- Market impact, Python bindings, multithreading, and further performance optimization are not implemented.

## Remaining optimization candidates
- Canonical trace materialization cost, now measured separately.
- `OrderBook::apply()` and ordered-map mutation path.
- `MarketEvent` dispatch and typed payload access.
- EventLoop setup/copy boundaries around preloaded replay.
- Parser/artifact I/O measurements outside the core replay path.

## Next phase
Phase 12B has not been started.

## Verification commands
```bash
$ sed -n '1,260p' attached Phase 12A request
Result: PASS - Phase 12A request read.

$ sed -n '261,520p' attached Phase 12A request
Result: PASS - Phase 12A request read completely.

$ sed -n '1,400p' PROJECT_SPEC.md
Result: PASS - spec read started.

$ sed -n '401,800p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '801,1200p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1201,1600p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1601,2000p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '2001,2400p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '2401,2800p' PROJECT_SPEC.md
Result: PASS - spec read completed.

$ sed -n '1,340p' STATUS.md
Result: PASS - STATUS read before changes.

$ git status --short
Result: PASS - existing benchmark decomposition working-tree changes observed; no staged changes.

$ python3 - <<'PY'
text = "M,100,book_update,1\nM,100,trade,2\nI,100,user,1,at100\nI,125,order_arrival,2,at125\nM,150,book_update,3\nI,150,timer,0,at150\nM,175,book_update,4\n"
h = 14695981039346656037
for b in text.encode():
    h ^= b
    h = (h * 1099511628211) & ((1 << 64) - 1)
print(text, end="")
print(f"hash={h:016x}")
PY
Result: PASS - captured representative pre-refactor canonical trace text and hash `0e1151708630d39a`.

$ cmake --build build
Result: PASS - initial Debug rebuild after trace refactor.

$ ctest --test-dir build --output-on-failure
Result: PASS - initial Debug CTest run passed 18/18 tests.

$ cmake --build build-release
Result: PASS - Release build for Phase 12A benchmark.

$ ./build-release/benchmark_replay --output-dir benchmarks/results --scales 100000,1000000 --repetitions 5 --warmups 1
Result: PASS - wrote `baseline_summary.csv` and `baseline_repetitions.csv`; all hashes/checksums stable.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - Release CTest run passed 18/18 tests.

$ cmake --build build-asan
Result: PASS - initial ASan/UBSan rebuild after trace refactor.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - initial ASan/UBSan CTest run passed 18/18 tests.

$ ./build-release/replay_cli --config configs/example_config.kv --output artifacts/example_run_release --force
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - final Debug configure.

$ cmake --build build
Result: PASS - final Debug build.

$ ctest --test-dir build --output-on-failure
Result: PASS - final Debug CTest run passed 18/18 tests.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - final Release configure.

$ cmake --build build-release
Result: PASS - final Release build.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - final Release CTest run passed 18/18 tests.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - final ASan/UBSan configure.

$ cmake --build build-asan
Result: PASS - final ASan/UBSan build.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - final ASan/UBSan CTest run passed 18/18 tests.

$ cmake --build build
Result: PASS - final Debug rebuild after benchmark formatting cleanup.

$ cmake --build build-release
Result: PASS - final Release rebuild after benchmark formatting cleanup.

$ cmake --build build-asan
Result: PASS - final ASan/UBSan rebuild after benchmark formatting cleanup.

$ ctest --test-dir build --output-on-failure
Result: PASS - final Debug CTest after formatting cleanup passed 18/18 tests.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - final Release CTest after formatting cleanup passed 18/18 tests.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - final ASan/UBSan CTest after formatting cleanup passed 18/18 tests.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!artifacts/**' -g '!benchmarks/results/e2e_repetition_*/**' -g '!.git/**'
Result: PASS - no absolute local-machine paths found in public files.

$ git status --short
Result: PASS - only Phase 12A files are modified; no staged changes.

$ git status --ignored --short
Result: PASS - private spec, generated artifacts, benchmark E2E artifact directories, and build directories are ignored.
```
