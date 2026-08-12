# Benchmarks

## Purpose

Phase 11 establishes a single-threaded Release baseline before any optimization. The goal is measurement, not speedup.
No production hot paths, container choices, parser logic, event semantics, hashing semantics, allocators, SIMD, or
threading were changed for these results.

## Benchmarks

`benchmark_replay` is a repository-native deterministic harness.

- `order_book_apply`: prepares `BookUpdateEvent` objects before timing, then times only `OrderBook::apply()`.
- `bare_event_iteration`: iterates over the preloaded `MarketEvent` vector. It performs no `MarketEvent` accessor calls,
  no event-type dispatch, and no book mutation.
- `event_type_dispatch_only`: iterates over the same preloaded `MarketEvent` vector, performs the existing
  `MarketEvent::type()` dispatch, and reads the typed payload. It performs no book mutation and does not construct an
  `EventLoop` trace.
- `core_preloaded_replay`: prepares typed `MarketEvent` objects before timing, then times `EventLoop` processing with a
  historical `OrderBook`.
- `end_to_end_public_example`: measures the public Phase 10 example through `ReplayEngine`, including config/input
  loading, strategy/execution/accounting, and artifact writing. It is labeled separately and should not be compared
  directly with the preloaded replay benchmark.

No trace-disabled `EventLoop` benchmark was added. The production `EventLoop::run()` currently records trace entries as
part of its normal result. Disabling that path could not be isolated through an existing benchmark/test-only
configuration without changing normal production behavior, so that requested optional variant was skipped.

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
  Non-mutating iteration and dispatch-only variants report deterministic guard checksums, not canonical book hashes.
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
| order_book_apply | 100,000 | 41,660,886.219 | 24.003 | 22.510 | 24.412 | `e3c154d22706031c` |
| bare_event_iteration | 100,000 | 637,958,532.695 | 1.567 | 1.567 | 1.580 | `a4445da1d4c19ace` |
| event_type_dispatch_only | 100,000 | 94,343,271.768 | 10.600 | 10.440 | 10.681 | `55d684c8b390e7f1` |
| core_preloaded_replay | 100,000 | 26,714,453.481 | 37.433 | 36.186 | 39.450 | `7881be7964fe68e6` |
| order_book_apply | 1,000,000 | 45,301,053.132 | 22.075 | 21.785 | 22.248 | `a7145c79f43da48a` |
| bare_event_iteration | 1,000,000 | 633,395,659.466 | 1.579 | 1.552 | 1.618 | `b43a3a668619fe0e` |
| event_type_dispatch_only | 1,000,000 | 88,443,722.154 | 11.307 | 10.644 | 12.486 | `44a3decf7d24b334` |
| core_preloaded_replay | 1,000,000 | 24,639,240.859 | 40.586 | 39.255 | 41.239 | `2855ca09d92eee1f` |
| end_to_end_public_example | 5 | 3,920.157 | 255,091.800 | 248,975.000 | 270,125.000 | `8aca37583ca6f83a` |

Per-repetition CSV: `benchmarks/results/baseline_repetitions.csv`

## Variability

For the 1M headline runs:

- `order_book_apply`: 21.785 to 22.248 ns/event.
- `bare_event_iteration`: 1.552 to 1.618 ns/event.
- `event_type_dispatch_only`: 10.644 to 12.486 ns/event.
- `core_preloaded_replay`: 39.255 to 41.239 ns/event.

All repetitions produced identical functional hashes or guard checksums for their benchmark and scale.

## Coarse 1M Replay Decomposition

The following rows are the benchmark-only decomposition requested after the Phase 11 baseline. Performance numbers are
from the Release build only, with one warmup and five measured repetitions.

| Variant | Directly comparable workload | Median events/sec | Median ns/event | Hash/checksum | Notes |
|---|---|---:|---:|---|---|
| bare_event_iteration | Same 1M mixed `MarketEvent` vector as core replay | 633,395,659.466 | 1.579 | `b43a3a668619fe0e` | Iterates vector elements only; no accessor calls, no dispatch, no mutation, no trace. |
| event_type_dispatch_only | Same 1M mixed `MarketEvent` vector as core replay | 88,443,722.154 | 11.307 | `44a3decf7d24b334` | Dispatches and reads typed payloads; no mutation, no trace. |
| order_book_apply | Not directly comparable to mixed core replay | 45,301,053.132 | 22.075 | `a7145c79f43da48a` | Existing direct `OrderBook::apply()` reference over 1M book updates. |
| core_preloaded_replay | Same 1M mixed `MarketEvent` vector as iteration/dispatch rows | 24,639,240.859 | 40.586 | `2855ca09d92eee1f` | Full current Phase 11 headline baseline: EventLoop construction, dispatch, trace collection, and book mutation. |
| EventLoop + OrderBook with trace disabled | Not run | N/A | N/A | N/A | No safe benchmark/test-only switch exists; changing it would alter normal EventLoop behavior. |

The direct `order_book_apply` row applies 1M book updates, while the mixed core replay workload contains 800K book
updates and 200K trades. It remains the reference microbenchmark for book mutation cost, but it is not an event-for-event
replacement for full core replay.

Using the comparable 1M mixed `MarketEvent` rows, dispatch and payload access add about 9.7 ns/event over bare iteration.
The full current core replay adds about 29.3 ns/event over dispatch-only. That remainder includes copying the preloaded
event vector into `EventLoop`, EventLoop loop/control flow, trace entry construction and storage, and `OrderBook`
mutation for the 800K update events. This is a coarse decomposition only; it is not a CPU-sampled profile and it does
not isolate trace collection separately.

## Profiling Method

The macOS `sample` profiler was attempted against a longer 1M-event Release benchmark process, but the sandbox denied
process inspection without elevated permissions. `/usr/bin/time -l` also returned real/user/sys timing but could not
provide extended memory data because `sysctl` access was restricted.

Fallback evidence uses coarse component timing from the same Release harness:

- 1M `bare_event_iteration` median: 1.579 ns/event.
- 1M `event_type_dispatch_only` median: 11.307 ns/event.
- 1M `order_book_apply` median: 22.075 ns/event.
- 1M `core_preloaded_replay` median: 40.586 ns/event.
- Direct book mutation remains the largest isolated mutating component, but the direct row uses 1M book updates and is
  not directly comparable to the 1M mixed replay row.
- EventLoop construction/dispatch, trace recording, market-event variant access, loop control, and book mutation account
  for the measured core replay overhead beyond the non-mutating dispatch-only row.

These are coarse timing observations, not CPU-sampled percentages.

## Measured Hotspots

Observed Phase 11 candidates:

1. `OrderBook::apply()` and its ordered-map insert/update/delete path.
   Evidence: direct book mutation remains the largest isolated mutating benchmark at 22.075 ns/event.
2. `EventLoop::run()` control flow and trace recording around preloaded market events.
   Evidence: full core replay is 40.586 ns/event while dispatch-only over the same mixed event vector is 11.307
   ns/event.
3. `MarketEvent` dispatch and payload access.
   Evidence: dispatch-only is 11.307 ns/event while bare iteration over the same vector is 1.579 ns/event.
4. End-to-end application overhead is dominated by layers outside the preloaded core path for the tiny public example,
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
- Trace collection could not be disabled through an existing benchmark/test-only configuration without changing normal
  EventLoop behavior, so no trace-disabled timing is reported.
- Non-mutating decomposition variants report deterministic guard checksums rather than canonical production hashes.
- End-to-end benchmark uses the tiny public golden scenario and is useful for application-level sanity, not headline
  throughput.
