# Execution Model

Phase 6 added execution-domain orders, fills, market execution, and deterministic fees. Phase 7 integrates market-order execution with the deterministic event scheduler so strategy decisions become pending orders and execute only at exchange arrival time.

This phase does not implement resting passive limit orders, cancel requests, cancellation latency or races, passive fills, portfolio accounting, PnL, market impact, benchmarking, Python bindings, or multithreading.

## OrderIntent vs Order

`OrderIntent` is a Phase 5 strategy decision. It has no order ID, lifecycle, fills, execution state, or portfolio effect.

`Order` is a Phase 6 execution-domain object created from an intent or direct simulator input. It contains:

- deterministic `OrderId`;
- `Side`;
- `OrderType`;
- original quantity;
- filled quantity;
- computed remaining quantity;
- decision timestamp;
- submit timestamp;
- exchange-arrival timestamp;
- optional limit price;
- current `OrderStatus`;
- status history.

`OrderIntent` conversion creates an `Order`, but no fill exists until an `OrderArrival` internal event is processed. Strategy code does not call execution directly.

## Timing Semantics

Phase 7 distinguishes:

- decision time: when the strategy emitted the `OrderIntent`;
- submit time: when the simulator accepted the intent as an order;
- exchange arrival time: when the order becomes eligible for acknowledgement and execution.

In the current implementation decision time and submit time are equal. Exchange arrival is:

```text
exchange_arrival_timestamp_ns = submit_timestamp_ns + configured_latency_ns
```

Latency is represented as `LatencyNs`, the same integer nanosecond unit used by the replay clock. User-facing conversions such as `LatencyNs::from_microseconds(50)` are explicit and reject negative values. Timestamp addition is checked; overflow throws `std::overflow_error` rather than wrapping.

## Scheduler Integration

Latency-aware execution uses the Phase 4 `EventLoop` internal scheduler. Submitting an intent:

1. creates a deterministic `Order`;
2. transitions it from `New` to `Pending`;
3. stores it in the latency execution registry;
4. schedules `InternalEventType::OrderArrival` with the order ID.

The order registry owns order state. The scheduled internal event carries only the order ID and a deterministic label; it does not carry an `OrderBook` snapshot or a future execution result.

At `OrderArrival`, execution reads the current historical `OrderBook` and then applies the Phase 6 market-order sweep rules.

## Zero-Latency Semantics

A latency of zero schedules exchange arrival at the current simulation timestamp; it does not bypass the deterministic event scheduler.

Phase 4 ordering still applies: historical market events at timestamp `T` are processed before internal events at timestamp `T`. Therefore, if a strategy submits an order with zero latency during a `t=100` market callback and later source-order market events also have `t=100`, those market events process before the order arrival.

Zero latency means scheduler timestamp equality, not inline execution inside a strategy callback.

## Order IDs

Order IDs are simulator-local monotonic integers:

```text
1, 2, 3, ...
```

No random UUID, wall-clock timestamp, memory address, or random device is used. Repeating the same order-creation sequence with a fresh factory produces the same IDs.

## Lifecycle Transition Table

Status changes go through `Order::transition_to`; callers cannot assign arbitrary status values.

Allowed transitions:

| Current | Allowed Next |
|---|---|
| `New` | `Pending` |
| `Pending` | `Acknowledged`, `Rejected` |
| `Acknowledged` | `PartiallyFilled`, `Filled`, `Canceled` |
| `PartiallyFilled` | `Filled`, `Canceled` |
| `Filled` | none |
| `Canceled` | none |
| `Rejected` | none |

Invalid transitions throw `std::invalid_argument`.

Market-order outcomes use these policies:

- full fill: `New -> Pending -> Acknowledged -> Filled`;
- partial fill with insufficient visible liquidity: `New -> Pending -> Acknowledged -> PartiallyFilled -> Canceled`;
- zero executable liquidity: `New -> Pending -> Rejected`.

During latency, an order remains `Pending` with zero filled quantity. No fill can occur before the `OrderArrival` internal event.

For partial market execution, `Canceled` means only the unfilled remainder was canceled after the arrival-time sweep. Existing fills remain valid. A market-order remainder never becomes a resting order in Phase 7.

## Market-Order Sweep Rules

Market orders walk the opposite side of the visible L2 book in price priority:

- Buy Market reads asks from lowest price to higher prices.
- Sell Market reads bids from highest price to lower prices.

Each consumed price level creates a deterministic `Fill`. Fill sequence IDs are simulator-local monotonic integers and reflect execution order.

Execution uses the book state at exchange arrival time. It does not use a decision-time snapshot, submit-time snapshot, stale book copied into the order, or future book state.

If the historical feed ends while an order is pending, the Phase 4 event loop continues processing internal events. The pending order arrives after feed exhaustion and executes against the final historical `OrderBook` state.

## Historical Book Immutability

The authoritative historical `OrderBook` represents observed market data. Simulated execution reads visible liquidity from a snapshot returned by the const book interface, but it does not erase, reduce, or otherwise mutate historical book levels.

This is a zero-market-impact baseline:

```text
Simulated execution reads historical visible liquidity but does not mutate the authoritative historical OrderBook.
Market impact is not modeled.
```

Because market impact is not modeled, multiple simulated market orders at the same arrival time may observe the same historical visible liquidity. The simulator does not secretly reduce historical depth to make simulated orders compete.

## Fill Records

`Fill` contains:

- `OrderId`;
- `Side`;
- `PriceTicks`;
- `Quantity`;
- fill timestamp;
- deterministic fill sequence ID;
- fee amount.

Fill price is always an integer `PriceTicks` level from historical visible depth. No `double` or `float` price is stored in canonical order or fill state.

## Quantity Conservation

For every order:

```text
0 <= filled_quantity <= original_quantity
remaining_quantity = original_quantity - filled_quantity
sum(fill.quantity for order) == order.filled_quantity
```

Overfills and negative quantities are rejected explicitly.

## Fee Model

Phase 6 uses one taker-style fixed-point fee rate represented in parts per million. Valid rates are in `[0, 1,000,000]`.

```text
fee_rate_ppm = 100 means 100 / 1,000,000
```

The canonical notional unit is:

```text
notional_tick_quantity = price_ticks * quantity
```

Fees are calculated per fill:

```text
fee_amount = round_half_up(notional_tick_quantity * fee_rate_ppm / 1,000,000)
```

`fee_amount` is stored as integer `FeeAmount` in the same tick-quantity notional unit. The calculation uses integer arithmetic with checked intermediate range; no hidden floating-point rounding is used.

## Limit-Order Policy

`OrderType::Limit` is represented structurally for future compatibility. Phase 7 does not implement marketable limit execution, resting orders, cancels, passive queue fills, or queue-ahead modeling.

Submitting a limit order to `ExecutionSimulator::execute_order` produces:

```text
New -> Pending -> Rejected
```

No fills are generated. This explicit rejection prevents accidental treatment of limits as market orders before Phase 8.

## Current Limitations

- No resting order book for simulated orders.
- No cancel request handling.
- No passive limit fills or L2 queue approximation.
- No portfolio, inventory, cash, PnL, fees-to-accounting, or equity logic.
- No market impact.
