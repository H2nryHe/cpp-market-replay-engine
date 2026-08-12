# Project Status

## Current phase
Phase 14 - CI, Documentation & Portfolio Release

## Phase status
PASS

## Last verified commit
0924bee

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS
- Python binding Release: PASS
- Clean-copy Release: PASS
- Clean-copy Python binding Release: PASS

## Tests
- CTest Debug: 18/18 passed
- CTest Release: 18/18 passed
- CTest ASan/UBSan: 18/18 passed
- CTest Release with Python bindings: 19/19 passed
- Clean-copy CTest Release: 18/18 passed
- Clean-copy CTest Release with Python bindings: 19/19 passed
- CLI help: PASS
- Public CLI synthetic example: PASS
- Python import: PASS, `market_replay.__version__ == "0.1.0"`
- Python example: PASS
- Golden final book hash: PASS, `9ca1786003897355`
- Golden run hash: PASS, `8aca37583ca6f83a`
- Python 100-run repeatability: PASS through `python_bindings_tests`
- Python/C++ golden equivalence: PASS through `python_bindings_tests`

## Benchmarks
- Frozen Phase 11 1M core replay baseline: 24,639,240.859 events/sec, 40.586 ns/event.
- Phase 12A optimized 1M core replay baseline: 36,647,096.995 events/sec, 27.287 ns/event.
- Documented Phase 12A improvement: 1.487x, 48.735%.
- Phase 13 native regression check: 37,365,193.388 events/sec, 26.763 ns/event; interpreted as no material regression.
- Phase 14 native regression check: PASS after rerun, 35,851,234.871 events/sec, 27.893 ns/event, hash
  `2855ca09d92eee1f`.

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
- Phase 12 - PASS
- Phase 13 - PASS
- Phase 14 - PASS

## Phase 14 implementation summary
- Added GitHub Actions CI for native Release C++, native ASan/UBSan C++, and optional pybind11 Python binding builds.
- Rewrote README as a recruiter-facing public entry point with concise purpose, highlights, quick start, architecture,
  benchmark story, deterministic hashes, Python binding instructions, documentation index, limitations, and release note.
- Added `docs/architecture.md` and `docs/determinism.md`.
- Updated execution-model docs to remove stale phase wording about Python bindings while preserving model boundaries.
- Centralized the compiled engine version through the CMake project version by defining `REPLAY_ENGINE_VERSION` for the
  C++ library. Python still reads the C++ engine version and reports `0.1.0`.
- Removed the tracked internal AI execution-spec copy `cpp-market-replay-engine_PROJECT_SPEC.md` from the public release
  tree and added a narrow ignore rule for local spec copies.
- Fixed CTest output paths so clean builds with arbitrary binary directories do not depend on a preexisting source-root
  `build/` directory.
- Did not add trading-engine features, strategies, performance optimizations, multithreading, or semantic changes.

## CI workflow design
- `native`: Ubuntu Release configure/build/CTest, `replay_cli --help`, and golden CLI hash check.
- `sanitizers`: Ubuntu Debug configure/build/CTest with `ENABLE_SANITIZERS=ON`.
- `python`: Ubuntu Python setup, `pybind11` install, `BUILD_PYTHON_BINDINGS=ON`, CTest including
  `python_bindings_tests`, import version check, and Python example hash check.
- CI uses CMake/Python discovery and does not rely on local paths, private datasets, or platform-specific sanitizer
  loader hacks.

## README and docs changes
- README first screen now states what the project is, why it exists, measured speed, deterministic trust signals, and
  Python/C++ boundary.
- README includes copy-paste native build/test/example commands and optional Python binding commands.
- README documents real example output values and the preserved golden hashes.
- README benchmark section preserves the verified Phase 11, Phase 12A, and Phase 13 numbers without presenting Phase 13
  as another optimization.
- Documentation index links to the actual files under `docs/`.
- `LICENSE` exists and remains MIT.

## Clean-copy verification
- A public-only temporary copy was created with `.git/`, private spec files, build directories, generated artifacts,
  Python caches, and ignored outputs excluded.
- Native clean-copy Release configure/build/CTest/example passed.
- Optional clean-copy Python binding configure/build/CTest/import/example passed using pybind11 3.1.0 installed outside
  the repository.
- Initial clean-copy CTest exposed a source-root `build/` directory assumption in tests; CTest paths and integration test
  output parent handling were fixed, then clean-copy verification passed.

## Repository and privacy audit
- `PROJECT_SPEC.md` is ignored and not required for public build/test/example workflows.
- The tracked internal spec copy is deleted in this phase.
- Generated artifact directories remain ignored except `artifacts/.gitkeep`.
- Build directories, Python caches, virtual environments, binary outputs, and local benchmark E2E artifact directories
  are ignored.
- No tracked build binaries, `.DS_Store`, Python caches, API keys, tokens, credentials, private datasets, usernames, or
  absolute local machine paths were found in the public tracked file scan.
- Release-appropriate limitation phrases such as "not exact FIFO" and "not profitable" remain intentionally present as
  fidelity disclaimers.

## Known limitations
- GitHub Actions was added but not executed remotely in this local environment; local equivalent configure/build/test
  commands passed.
- Linux CI is configured; macOS CI is intentionally omitted to keep the portfolio workflow small and reliable.
- Python sanitizer-extension import is not required because platform loaders can reject sanitizer runtimes; C++ sanitizer
  CTest is the required sanitizer gate.
- The 10M benchmark scale was not run.
- Benchmark timing remains machine and load dependent; functional hashes are the correctness gate.
- Phase 15 multithreading has not been started.

## Recommended release tag
`v0.1.0`

Do not tag or push automatically without explicit authorization.

## Next phase
Phase 15 is optional and has not been started.

## Verification commands

```bash
$ sed -n '1,260p' attached Phase 14 request
Result: PASS - Phase 14 request read.

$ sed -n '261,560p' attached Phase 14 request
Result: PASS - Phase 14 request read continued.

$ sed -n '561,900p' attached Phase 14 request
Result: PASS - Phase 14 request read completely.

$ sed -n '1,450p' PROJECT_SPEC.md
$ sed -n '451,900p' PROJECT_SPEC.md
$ sed -n '901,1350p' PROJECT_SPEC.md
$ sed -n '1351,1800p' PROJECT_SPEC.md
$ sed -n '1801,2250p' PROJECT_SPEC.md
$ sed -n '2251,2800p' PROJECT_SPEC.md
Result: PASS - PROJECT_SPEC.md read completely before changes.

$ sed -n '1,260p' STATUS.md
$ sed -n '261,560p' STATUS.md
Result: PASS - STATUS.md read completely before changes.

$ git status --short
Result: PASS - worktree clean before Phase 14 edits.

$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug configure.

$ cmake --build build
Result: PASS - Debug build.

$ ctest --test-dir build --output-on-failure
Result: PASS - Debug CTest passed 18/18 tests.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release configure.

$ cmake --build build-release
Result: PASS - Release build.

$ ctest --test-dir build-release --output-on-failure
Result: PASS - Release CTest passed 18/18 tests.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - ASan/UBSan configure.

$ cmake --build build-asan
Result: PASS - ASan/UBSan build.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - ASan/UBSan CTest passed 18/18 tests.

$ ./build-release/replay_cli --config configs/example_config.kv --output artifacts/phase14_example_run --force
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON -Dpybind11_DIR="$(PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c 'import pybind11; print(pybind11.get_cmake_dir())')"
Result: PASS - Python binding configure.

$ cmake --build build-python
Result: PASS - Python binding build.

$ ctest --test-dir build-python --output-on-failure
Result: PASS - Python binding CTest passed 19/19 tests.

$ PYTHONPATH=build-python python3 -c "import market_replay; print(market_replay.__version__)"
Result: PASS - `0.1.0`.

$ PYTHONPATH=build-python python3 examples/python_replay.py
Result: PASS - final book hash `9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ python3 --version
Result: PASS - `Python 3.9.6`.

$ ./build-release/benchmark_replay --output-dir build/phase14_benchmark --scales 1000000 --repetitions 5 --warmups 1
Result: PASS with functional hashes stable; first median `core_preloaded_replay` was 34,142,025.911 events/sec and
29.289 ns/event, treated as local timing noise after rerun.

$ ./build-release/benchmark_replay --output-dir build/phase14_benchmark_rerun --scales 1000000 --repetitions 5 --warmups 1
Result: PASS - rerun median `core_preloaded_replay` was 35,851,234.871 events/sec and 27.893 ns/event with hash
`2855ca09d92eee1f`; no material regression against Phase 12A.

$ ./build-release/replay_cli --help
Result: PASS - printed usage and version `0.1.0`.

$ tmp=$(mktemp -d /tmp/cpp-market-replay-clean.XXXXXX)
$ rsync -a --exclude '.git/' --exclude 'PROJECT_SPEC.md' --exclude '*_PROJECT_SPEC.md' --exclude 'build/' --exclude 'build-*/' --include 'artifacts/.gitkeep' --exclude 'artifacts/*' --exclude '__pycache__/' --exclude '*.pyc' ./ "$tmp"/
$ cd "$tmp"
$ test ! -e PROJECT_SPEC.md
$ test ! -e cpp-market-replay-engine_PROJECT_SPEC.md
$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
$ cmake --build build-release
$ ctest --test-dir build-release --output-on-failure
$ ./build-release/replay_cli --config configs/example_config.kv --output artifacts/clean_example_run --force
Result: PASS - clean-copy Release CTest passed 18/18 tests; final book hash `9ca1786003897355`; run hash
`8aca37583ca6f83a`.

$ cmake -S . -B build-python -DCMAKE_BUILD_TYPE=Release -DBUILD_PYTHON_BINDINGS=ON -Dpybind11_DIR="$(PYTHONPATH=/tmp/cpp_market_replay_pydeps python3 -c 'import pybind11; print(pybind11.get_cmake_dir())')"
$ cmake --build build-python
$ ctest --test-dir build-python --output-on-failure
$ PYTHONPATH=build-python python3 -c "import market_replay; print(market_replay.__version__)"
$ PYTHONPATH=build-python python3 examples/python_replay.py
Result: PASS - clean-copy Python binding CTest passed 19/19 tests; import version `0.1.0`; final book hash
`9ca1786003897355`; run hash `8aca37583ca6f83a`.

$ rg -n "<private-path, credential, and unsupported-claim patterns>" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-*/**' -g '!artifacts/**' -g '!.git/**'
Result: PASS - remaining matches are intentional public statements about ignored spec files or model limitations; no
private paths, credentials, or unsupported claims found.

$ git ls-files -d
Result: PASS - `cpp-market-replay-engine_PROJECT_SPEC.md` is intentionally deleted for public release.

$ git ls-files -i -c --exclude-standard
Result: PASS - only the intentionally deleted tracked spec copy is reported while this phase remains uncommitted.

$ git status --short
Result: PASS - working-tree changes are Phase 14 release-hardening files only.
```
