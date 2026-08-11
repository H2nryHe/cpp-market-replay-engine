# Event Loop

Phase 4 implements deterministic single-threaded simulation time, internal scheduling, and event-loop dispatch. It does not implement strategies, orders, fills, execution, latency modeling, portfolio accounting, benchmarking, Python bindings, multithreading, or asynchronous execution.

## SimulationClock

`SimulationClock` uses `TimestampNs` and starts at `0` by default unless explicitly constructed with another initial timestamp. The clock advances only through `advance_to(timestamp_ns)`.

- advancing to a future timestamp succeeds;
- advancing to the current timestamp succeeds;
- advancing backward throws;
- no wall-clock time is used for simulation ordering.

## Internal Scheduler

The scheduler accepts generic `InternalEvent` payloads with lightweight categories such as `Timer`, `OrderArrival`, `CancelArrival`, and `User`. These are event categories only; no order, cancel, fill, or execution behavior is implemented in this phase.

Scheduling rules:

- events in the future are accepted;
- events at the current simulation time are accepted;
- events in the past are rejected;
- duplicate payloads are allowed;
- each scheduled event receives a deterministic monotonic `internal_sequence_id`.

Multiple internal events at the same timestamp are processed in insertion order.

## Total Event Ordering

The event loop merges the Phase 2 historical market stream with internally scheduled events using this total ordering:

```text
timestamp_ns ascending
then event class priority
then source or internal sequence
```

At the same timestamp, historical market events are processed before internal scheduled events. This conservative policy avoids treating internal events as if they could observe a timestamp-colliding market update before the market event has been dispatched.

Among historical market events, the event loop preserves the Phase 2 source ordering and `EventKey` contract. It does not silently sort, rewrite, or repair the historical feed.

Among internal events at the same timestamp, the scheduler uses the deterministic `internal_sequence_id` assigned when the event is scheduled.

## Dispatch

Market events and internal events are dispatched through simple testable handler hooks. If an `OrderBook` pointer is provided, `BookUpdateEvent` objects are applied to it during market dispatch. `TradeEvent` objects are recorded/dispatched but do not mutate the book because no trade-driven book mutation is defined in the current data contract.

Handlers may schedule additional internal events. Scheduling at the current simulation time is allowed and those events are processed according to the same ordering rules.

## End Of Stream

The loop continues until both conditions are true:

- the historical market stream is exhausted;
- the internal scheduler is empty.

Therefore, pending internal events after the last market event are still processed.

## Deterministic Trace

Every processed event is recorded in a canonical trace:

```text
M,<timestamp_ns>,<market_event_type>,<market_sequence_id>
I,<timestamp_ns>,<internal_event_type>,<internal_sequence_id>,<label>
```

`EventLoopResult::trace_hash()` applies FNV-1a 64-bit to the canonical trace. The trace hash is a deterministic regression checksum, not a cryptographic claim.

## Current Limitations

This phase provides the event-processing kernel only. It does not implement strategy callbacks, execution, order lifecycle, latency-aware fills, portfolio accounting, artifacts, or performance benchmarking.
