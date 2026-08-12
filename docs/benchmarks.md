# Benchmarks

## Purpose

Phase 11 established the single-threaded Release baseline before optimization. Phase 12A performs one targeted
optimization experiment based on the coarse replay decomposition: reduce trace-related hot-loop overhead while preserving
deterministic trace observability and all replay semantics.

No `OrderBook` containers, parser logic, event ordering, execution semantics, canonical production hashes, custom
allocators, SIMD, or threading were changed for Phase 12A.

## Frozen Phase 11 Baseline

The pre-optimization 1M-event `core_preloaded_replay` baseline is retained for comparison:

| Baseline | Events/sec | ns/event | Functional hash |
|---|---:|---:|---|
| Phase 11 `core_preloaded_replay` | 24,639,240.859 | 40.586 | `2855ca09d92eee1f` |

This is the comparison point for Phase 12A. It was collected on the same deterministic 1M mixed `MarketEvent` workload
with one warmup and five measured repetitions.

## Phase 12A Hypothesis

Phase 11 showed that full replay had substantial overhead beyond direct `OrderBook::apply()` timing, and the
trace-disabled EventLoop path could not be isolated safely without changing production behavior. The Phase 12A hypothesis
was that the trace representation itself contributed to the replay hot path through a larger optional-heavy record shape
and avoidable trace-vector growth.

This was targeted based on coarse benchmark decomposition, not sampling-profiler attribution.

## Implementation Change

The EventLoop trace path now appends compact typed records:

- market trace records store event class, trace kind, timestamp, and market sequence id;
- internal trace records store event class, trace kind, timestamp, internal sequence id, and the existing label;
- optional market/internal fields were removed from each stored trace entry;
- trace vector capacity is reserved up front for the known historical event count;
- canonical trace text remains deferred until `canonical_trace()` or `canonical_trace_line()` is requested.

Canonical trace field ordering, separators, labels, trace hash algorithm, final book hash, artifact hashes, and run hash
were preserved.

## Benchmark Variants

`benchmark_replay` is a repository-native deterministic harness.

- `bare_event_iteration`: iterates over the preloaded `MarketEvent` vector with no accessor calls, dispatch, trace, or
  book mutation.
- `event_type_dispatch_only`: performs existing `MarketEvent::type()` dispatch and typed payload reads over the same
  mixed event vector, with no book mutation and no EventLoop trace.
- `order_book_apply`: direct `OrderBook::apply()` reference over generated `BookUpdateEvent` objects.
- `core_preloaded_replay`: optimized EventLoop replay hot path with normal compact trace collection and `OrderBook`
  mutation, but without canonical trace text materialization.
- `trace_materialization_only`: canonical trace text generation from completed trace records.
- `core_replay_with_trace_materialization`: replay plus full canonical trace text materialization.
- `end_to_end_public_example`: tiny Phase 10 public example through `ReplayEngine`, including I/O and artifact writing.

The direct `order_book_apply` row applies 1M book updates, while the mixed core replay workload contains 800K book
updates and 200K trades. It remains the reference microbenchmark for book mutation cost but is not an event-for-event
replacement for full core replay.

## Methodology

- Build mode: Release.
- Release flags from CMake cache: `-O3 -DNDEBUG`.
- Timed clock: `std::chrono::steady_clock`.
- Warmup: 1 untimed run for each benchmark/scale.
- Repetitions: 5 measured repetitions.
- Headline result: median batch run.
- Required metrics: events/sec and ns/event.
- Data generation is outside the timed region for preloaded benchmarks.
- Functional hashes are verified after timing where state mutation occurs.
- Non-mutating variants report deterministic guard checksums rather than canonical production hashes.

The 10M scale was not run. The 1M scale is the primary comparison point for Phase 12A.

## Environment

- Date: 2026-08-12
- Git commit: `fbfb48f`
- Working tree clean at benchmark time: false
- OS: macOS 26.5.1, Darwin 25.5.0, arm64
- Hardware model: MacBook Air, Mac14,2
- Chip: Apple M2
- Cores: 8 total, 4 performance and 4 efficiency
- Memory: 8 GB
- Compiler: Apple clang 21.0.0 (`clang-2100.1.1.101`)
- CMake build type: Release

`sysctl` and `sample` access were restricted by the execution sandbox. Hardware details were captured with
`system_profiler`; serial numbers and hardware UUIDs are intentionally omitted from this public document.

## Phase 12A Results

Summary CSV: `benchmarks/results/baseline_summary.csv`

| Benchmark | Events | Median events/sec | Median ns/event | Min ns/event | Max ns/event | Hash/checksum |
|---|---:|---:|---:|---:|---:|---|
| order_book_apply | 100,000 | 27,605,557.440 | 36.225 | 23.690 | 72.434 | `e3c154d22706031c` |
| bare_event_iteration | 100,000 | 636,602,072.776 | 1.571 | 1.565 | 3.730 | `a4445da1d4c19ace` |
| event_type_dispatch_only | 100,000 | 88,580,549.837 | 11.289 | 10.903 | 11.704 | `55d684c8b390e7f1` |
| core_preloaded_replay | 100,000 | 34,980,323.568 | 28.587 | 27.064 | 30.084 | `7881be7964fe68e6` |
| trace_materialization_only | 100,000 | 3,934,039.223 | 254.192 | 251.349 | 273.107 | `e1a948341b11f657` |
| core_replay_with_trace_materialization | 100,000 | 3,531,530.725 | 283.163 | 279.033 | 286.790 | `7881be7964fe68e6:e1a948341b11f657` |
| order_book_apply | 1,000,000 | 46,414,838.935 | 21.545 | 21.140 | 21.963 | `a7145c79f43da48a` |
| bare_event_iteration | 1,000,000 | 667,074,026.539 | 1.499 | 1.483 | 1.556 | `b43a3a668619fe0e` |
| event_type_dispatch_only | 1,000,000 | 97,203,380.967 | 10.288 | 10.182 | 10.540 | `44a3decf7d24b334` |
| core_preloaded_replay | 1,000,000 | 36,647,096.995 | 27.287 | 26.078 | 27.922 | `2855ca09d92eee1f` |
| trace_materialization_only | 1,000,000 | 3,798,326.574 | 263.274 | 260.414 | 267.933 | `7b159670a635cca7` |
| core_replay_with_trace_materialization | 1,000,000 | 3,363,864.146 | 297.277 | 287.601 | 303.583 | `2855ca09d92eee1f:7b159670a635cca7` |
| end_to_end_public_example | 5 | 3,490.401 | 286,500.000 | 221,000.000 | 463,916.600 | `8aca37583ca6f83a` |

Per-repetition CSV: `benchmarks/results/baseline_repetitions.csv`

## Phase 12A Comparison

| Scope | Events/sec | ns/event |
|---|---:|---:|
| Frozen Phase 11 hot replay baseline | 24,639,240.859 | 40.586 |
| Phase 12A optimized hot replay | 36,647,096.995 | 27.287 |
| Phase 12A replay + canonical trace materialization | 3,363,864.146 | 297.277 |
| Phase 12A trace materialization only | 3,798,326.574 | 263.274 |

Hot replay changed from 40.586 ns/event to 27.287 ns/event:

- absolute reduction: 13.299 ns/event;
- relative throughput improvement: 1.487x, or 48.735%;
- relative ns/event reduction: 32.767%.

The optimized hot replay row and the replay-plus-materialization row are intentionally separate. The former measures
normal replay processing with compact trace collection; the latter makes deferred canonical observability cost visible.
They should not be compared as if they measure the same scope.

## Functional Equivalence

- Canonical trace fixture bytes are unchanged.
- Canonical trace hash for the representative fixture remains `0e1151708630d39a`.
- 1M hot replay final book hash remains `2855ca09d92eee1f`.
- Phase 10 golden final book hash remains `9ca1786003897355`.
- Phase 10 golden run hash remains `8aca37583ca6f83a`.
- Existing CTest regression suites pass in Debug, Release, and ASan/UBSan configurations.

## Retained Decision

The Phase 12A refactor was retained. The 1M hot replay median improved beyond the measured run-to-run ranges, and all
semantic, hash, golden, and sanitizer checks passed. The result is reported as a targeted benchmark improvement, not as
sampling-profiler proof of exact CPU attribution.

## Remaining Bottleneck Candidates

Do not implement these as part of Phase 12A.

- Canonical trace text materialization is now explicitly measured and expensive when requested.
- `OrderBook::apply()` remains the largest isolated mutating microbenchmark.
- `MarketEvent` dispatch and payload access remain a separate measured candidate.
- Parser/artifact I/O should stay separate from core replay throughput measurements.

## Limitations

- Results were collected from a dirty working tree because benchmark harness, docs, and results are uncommitted.
- 10M scale was not run.
- OS-level sampling profiler output was unavailable in the sandbox.
- Memory statistics were not captured because the sandbox blocked the relevant system calls.
- Non-mutating decomposition variants report deterministic guard checksums rather than canonical production hashes.
- End-to-end benchmark uses the tiny public golden scenario and is useful for application-level sanity, not headline
  throughput.
