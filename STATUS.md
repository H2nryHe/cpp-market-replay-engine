# Project Status

## Current phase
Phase 13 - Python Binding with pybind11

## Phase status
PASS

## Last verified commit
fbfb48f

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS
- Python binding Release: PASS
- Python binding ASan/UBSan: BUILD PASS; Python import test unsupported by this macOS/Xcode Python sanitizer loader

## Tests
- CTest Debug: 18/18 passed
- CTest Release: 18/18 passed
- CTest ASan/UBSan: 18/18 passed
- CTest Release with Python bindings: 19/19 passed
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
- Python import: PASS, `market_replay.__version__ == "0.1.0"`
- Python golden API equivalence: PASS, final book hash `9ca1786003897355`, run hash `8aca37583ca6f83a`
- CLI vs Python artifact equivalence: PASS, `diff -r` reported no differences
- Python 100-run determinism regression: PASS through `python_bindings_tests`
- Python same-content different-path regression: PASS through `python_bindings_tests`
- Python invalid config, missing input, output-exists, optional mark `None`, exact integer fields, order/fill access, and
  failed-run recovery checks: PASS through `python_bindings_tests`

## Benchmarks
Phase 12A retained. Optimized 1M hot replay median: 36,647,096.995 events/sec, 27.287 ns/event.
Frozen Phase 11 baseline: 24,639,240.859 events/sec, 40.586 ns/event.
Phase 13 Release 1M regression check: PASS, 37,365,193.388 events/sec, 26.763 ns/event.

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
- Phase 12 - PASS (Phase 12A optimization)
- Phase 13 - PASS

## Phase 13 implementation summary
- Added an optional pybind11 extension target that imports as `market_replay`.
- Kept the default C++ build independent of Python by gating the extension behind `BUILD_PYTHON_BINDINGS`.
- Bound a thin Python API over the existing C++ `ReplayConfig`, `ReplayEngine::run()`, and
  `run_replay_from_config_file()` paths.
- Exposed Python-owned value snapshots for result, order, and fill data; Python does not receive mutable references to
  engine-owned vectors.
- Exposed canonical accounting, tick, quantity, timestamp, fee, and hash fields as exact Python integer/string values.
- Exposed unavailable optional mark/equity values as Python `None`.
- Added `examples/python_replay.py`.
- Updated README with build, import, example, integer/optional behavior, and limitations.
- Did not add Python strategy callbacks, Python replay loops, alternate accounting/order-book logic, multithreading, or
  production performance optimizations.

## Phase 13 pybind11 design
- Authoritative implementation remains the C++ replay engine.
- `market_replay.ReplayConfig.from_file(path)` calls existing `load_replay_config`.
- Typed `ReplayConfig()` construction maps to existing C++ config fields and validation where setters have immediate
  validation; full semantic validation still occurs in `ReplayEngine(config)`.
- `market_replay.ReplayEngine(config).run()` executes existing C++ replay and returns a snapshot `ReplayResult`.
- `market_replay.run_config_file(config_path, output_override=None, force_output=False)` uses the same C++ artifact path
  as the CLI.
- The binding releases the GIL only around C++ replay execution, with no Python callbacks involved.

## Phase 13 Python API
- Module: `market_replay`.
- Config: `ReplayConfig`, `ReplayConfig.from_file`, `load_config`, `canonical_config`.
- Engine: `ReplayEngine(config).run()`.
- Artifact path: `run_config_file(config_path, output_override=None, force_output=False)`.
- Enums: `Side`, `OrderType`, `OrderStatus`, `PriceFieldFormat`.
- Typed scripted config helpers: `EventKey`, `ScriptedIntent`.
- Result fields include event counts, order/fill counts, orders, fills, inventory, cash, realized PnL, fees, turnover,
  optional mark/equity fields, final book hash, and run hash.

## Phase 13 files changed
- `.gitignore`
- `CMakeLists.txt`
- `README.md`
- `examples/python_replay.py`
- `python/bindings.cpp`
- `tests/python/test_python_bindings.py`
- `STATUS.md`

## Phase 13 benchmark regression check
- Required baseline: Phase 12A 1M `core_preloaded_replay`, 36,647,096.995 events/sec, 27.287 ns/event.
- Phase 13 measurement: 37,365,193.388 events/sec, 26.763 ns/event.
- Result: PASS; no material regression observed.
- Final mutating benchmark hash for `core_preloaded_replay`: `2855ca09d92eee1f`.

## Phase 13 environment
- Date: 2026-08-12.
- Python: `Python 3.9.6`.
- pybind11: `3.1.0`.
- pybind11 CMake directory used for verification:
  `/tmp/cpp_market_replay_pydeps/pybind11/share/cmake/pybind11`.

## Phase 13 known limitations
- Python bindings require pybind11 to be available at configure time when `BUILD_PYTHON_BINDINGS=ON`.
- Python bindings expose replay orchestration and result inspection only; Python strategy callbacks are not implemented.
- The Python API does not provide a separate order book, market feed, parser, execution simulator, or portfolio
  implementation.
- ASan/UBSan Python-extension import was attempted but is unsupported in this environment because Apple’s Python loader
  rejected the ASan runtime with `Sanitizer load violates platform policy`. The same sanitizer configuration passed the
  18 C++ CTest tests.

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
- Market impact, Python strategy callbacks, multithreading, and further performance optimization are not implemented.

## Remaining optimization candidates
- Canonical trace materialization cost, now measured separately.
- `OrderBook::apply()` and ordered-map mutation path.
- `MarketEvent` dispatch and typed payload access.
- EventLoop setup/copy boundaries around preloaded replay.
- Parser/artifact I/O measurements outside the core replay path.

## Next phase
Phase 14 has not been started.

## Phase 13 verification commands
```bash
$ sed -n '1,260p' attached Phase 13 request
Result: PASS - Phase 13 request read.

$ sed -n '261,560p' attached Phase 13 request
Result: PASS - Phase 13 request read completely.

$ sed -n '1,450p' PROJECT_SPEC.md
Result: PASS - spec read started.

$ sed -n '451,900p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '901,1350p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1351,1800p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '1801,2250p' PROJECT_SPEC.md
Result: PASS - spec read continued.

$ sed -n '2251,2800p' PROJECT_SPEC.md
Result: PASS - spec read completed.

$ sed -n '1,340p' STATUS.md
Result: PASS - STATUS read before changes.

$ PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c "import pybind11; print(pybind11.__version__); print(pybind11.get_cmake_dir())"
Result: FAIL before dependency install - `ModuleNotFoundError: No module named 'pybind11'`.

$ python3 -m pip install --target /tmp/cpp_market_replay_pydeps pybind11
Result: FAIL under sandbox network restriction - DNS resolution failed for PyPI.

$ python3 -m pip install --target /tmp/cpp_market_replay_pydeps pybind11
Result: PASS with approved escalation - installed pybind11 3.1.0 into `/tmp/cpp_market_replay_pydeps`.

$ PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c "import pybind11; print(pybind11.__version__); print(pybind11.get_cmake_dir())"
Result: PASS - pybind11 3.1.0; CMake dir `/tmp/cpp_market_replay_pydeps/pybind11/share/cmake/pybind11`.

$ cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON -Dpybind11_DIR="$(PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c 'import pybind11; print(pybind11.get_cmake_dir())')"
Result: PASS - Python binding Release configure.

$ cmake --build build-python
Result: PASS - Python binding Release build.

$ ctest --test-dir build-python --output-on-failure
Result: PASS - Python binding Release CTest passed 19/19 tests.

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug configure.

$ cmake --build build
Result: PASS - Debug build.

$ ctest --test-dir build --output-on-failure
Result: PASS - Debug CTest passed 18/18 tests.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - ASan/UBSan configure.

$ cmake --build build-asan
Result: PASS - ASan/UBSan build.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - ASan/UBSan CTest passed 18/18 tests.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release configure.

$ cmake --build build-release
Result: PASS - Release build.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - Release CTest passed 18/18 tests.

$ PYTHONPATH=build-python python3 -c "import market_replay; print(market_replay.__version__)"
Result: PASS - imported `market_replay`, version `0.1.0`.

$ PYTHONPATH=build-python python3 examples/python_replay.py
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ ./build-release/benchmark_replay --output-dir build/phase13_benchmark --scales 1000000 --repetitions 5 --warmups 1
Result: PASS - 1M `core_preloaded_replay` median 37,365,193.388 events/sec, 26.763 ns/event; hash
`2855ca09d92eee1f`.

$ ./build-release/replay_cli --config configs/example_config.kv --output build/phase13_cli_equiv --force
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ PYTHONPATH=build-python python3 -c "import market_replay; r = market_replay.run_config_file('configs/example_config.kv', 'build/phase13_python_equiv', True); print(r.final_book_hash); print(r.run_hash)"
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ diff -r build/phase13_cli_equiv build/phase13_python_equiv
Result: PASS - no differences.

$ python3 --version
Result: PASS - `Python 3.9.6`.

$ cmake -S . -B build-python-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON -DBUILD_PYTHON_BINDINGS=ON -Dpybind11_DIR="$(PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c 'import pybind11; print(pybind11.get_cmake_dir())')"
Result: PASS - Python binding ASan/UBSan configure.

$ cmake --build build-python-asan
Result: PASS - Python binding ASan/UBSan build.

$ ctest --test-dir build-python-asan --output-on-failure
Result: PARTIAL/UNSUPPORTED - 18/19 tests passed; `python_bindings_tests` failed at import because this macOS/Xcode
Python loader rejected the ASan runtime with `Sanitizer load violates platform policy`.
```

## Phase 12A verification commands
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
