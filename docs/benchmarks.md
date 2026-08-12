# Benchmarks

## Purpose

Phase 11 establishes a single-threaded Release baseline before any optimization. The goal is measurement, not speedup.
No production hot paths, container choices, parser logic, event semantics, hashing semantics, allocators, SIMD, or
threading were changed for these results.

## Benchmarks

`benchmark_replay` is a repository-native deterministic harness.

- `order_book_apply`: prepares `BookUpdateEvent` objects before timing, then times only `OrderBook::apply()`.
- `core_preloaded_replay`: prepares typed `MarketEvent` objects before timing, then times `EventLoop` processing with a
  historical `OrderBook`.
- `end_to_end_public_example`: measures the public Phase 10 example through `ReplayEngine`, including config/input
  loading, strategy/execution/accounting, and artifact writing. It is labeled separately and should not be compared
  directly with the preloaded replay benchmark.

## Workload

The synthetic generator is deterministic and pattern-based. It does not use `random_device`.

- Book-update workloads alternate bid/ask sides.
- Prices span 256 levels per side.
- Quantities are deterministic replacements.
- Every 23rd update deletes a level.
- Core replay uses the same generated update stream, with every fifth event represented as a trade.

This workload exercises inserts, replacements, deletes, multiple levels, bid and ask maps, EventLoop dispatch, and final
book hashing. It is not a claim that the data reproduce any specific exchange.

## Methodology

- Build mode: Release.
- Release flags from CMake cache: `-O3 -DNDEBUG`.
- Timed clock: `std::chrono::steady_clock`.
- Warmup: 1 untimed run for each headline benchmark.
- Repetitions: 5 measured repetitions.
- Headline result: median batch run.
- Required metrics: events/sec and ns/event.
- Functional checksum: final `OrderBook` hash for book/replay benchmarks and run hash for the end-to-end benchmark.
- Data generation is outside the timed region for `order_book_apply` and `core_preloaded_replay`.
- Hashing is verified after timing for the micro/core runs.

The 10M scale was not run in Phase 11. The 1M scale already exercises the core replay path with stable hashes and low
run-to-run variability in this interactive environment; 10M is left as a future manual run rather than inventing data.

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

## Baseline Results

Summary CSV: `benchmarks/results/baseline_summary.csv`

| Benchmark | Events | Median events/sec | Median ns/event | Min ns/event | Max ns/event | Functional hash |
|---|---:|---:|---:|---:|---:|---|
| order_book_apply | 100,000 | 43,403,568.989 | 23.040 | 22.144 | 26.867 | `e3c154d22706031c` |
| core_preloaded_replay | 100,000 | 25,711,349.478 | 38.893 | 37.365 | 40.727 | `7881be7964fe68e6` |
| order_book_apply | 1,000,000 | 46,764,482.376 | 21.384 | 21.357 | 21.671 | `a7145c79f43da48a` |
| core_preloaded_replay | 1,000,000 | 26,226,068.712 | 38.130 | 37.821 | 38.373 | `2855ca09d92eee1f` |
| end_to_end_public_example | 5 | 6,366.045 | 157,083.400 | 124,800.000 | 338,666.600 | `8aca37583ca6f83a` |

Per-repetition CSV: `benchmarks/results/baseline_repetitions.csv`

## Variability

For the 1M headline runs:

- `order_book_apply`: 21.357 to 21.671 ns/event.
- `core_preloaded_replay`: 37.821 to 38.373 ns/event.

All repetitions produced identical functional hashes for their benchmark and scale.

## Profiling Method

The macOS `sample` profiler was attempted against a longer 1M-event Release benchmark process, but the sandbox denied
process inspection without elevated permissions. `/usr/bin/time -l` also returned real/user/sys timing but could not
provide extended memory data because `sysctl` access was restricted.

Fallback evidence uses coarse component timing from the same Release harness:

- 1M `order_book_apply` median: 21.384 ns/event.
- 1M `core_preloaded_replay` median: 38.130 ns/event.
- Direct book mutation therefore accounts for roughly 56% of the measured core replay batch time.
- The remaining batch time is EventLoop construction/dispatch, trace recording, market-event variant access, and loop
  control around the already-measured book mutation path.

These are coarse timing observations, not CPU-sampled percentages.

## Measured Hotspots

Observed Phase 11 candidates:

1. `OrderBook::apply()` and its ordered-map insert/update/delete path.
   Evidence: direct book mutation is the largest measured component in the core replay comparison.
2. `EventLoop::run()` dispatch and trace recording around preloaded market events.
   Evidence: core replay adds about 16.7 ns/event over direct book mutation at 1M scale.
3. End-to-end application overhead is dominated by layers outside the preloaded core path for the tiny public example,
   including config/input loading and artifact writing. It is not a replay-throughput benchmark.

## Candidate Phase 12 Targets

Do not implement these in Phase 11.

- Investigate `OrderBook` representation and `std::map` mutation cost.
- Measure whether trace recording should be configurable for benchmark/replay modes while preserving deterministic test
  traces.
- Investigate allocation/copy overhead in `EventLoop` setup for preloaded replay.
- Separate parser/artifact I/O measurements from core replay throughput before optimizing application-level runtime.

## Limitations

- Results were collected from a dirty working tree because Phase 11 benchmark harness and docs were uncommitted.
- 10M scale was not run.
- OS-level sampling profiler output was unavailable in the sandbox.
- Memory statistics were not captured because the sandbox blocked the relevant system calls.
- End-to-end benchmark uses the tiny public golden scenario and is useful for application-level sanity, not headline
  throughput.
