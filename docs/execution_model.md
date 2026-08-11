# Execution Model

Phase 6 adds execution-domain orders, fills, zero-latency market execution, and deterministic fees. It does not implement configurable latency, scheduled exchange arrival, resting passive limit orders, cancel races, passive fills, portfolio accounting, PnL, market impact, benchmarking, Python bindings, or multithreading.

## OrderIntent vs Order

`OrderIntent` is a Phase 5 strategy decision. It has no order ID, lifecycle, fills, execution state, or portfolio effect.

`Order` is a Phase 6 execution-domain object created from an intent or direct simulator input. It contains:

- deterministic `OrderId`;
- `Side`;
- `OrderType`;
- original quantity;
- filled quantity;
- computed remaining quantity;
- submit timestamp;
- exchange-arrival timestamp;
- optional limit price;
- current `OrderStatus`;
- status history.

Phase 6 exchange arrival is immediate: `exchange_arrival_timestamp_ns == submit_timestamp_ns`. Configurable delayed arrival belongs to Phase 7.

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

Immediate market-order outcomes use these policies:

- full fill: `New -> Pending -> Acknowledged -> Filled`;
- partial fill with insufficient visible liquidity: `New -> Pending -> Acknowledged -> PartiallyFilled -> Canceled`;
- zero executable liquidity: `New -> Pending -> Rejected`.

For partial market execution, `Canceled` means only the unfilled remainder was canceled after the immediate sweep. Existing fills remain valid. A market-order remainder never becomes a resting order in Phase 6.

## Market-Order Sweep Rules

Market orders walk the opposite side of the visible L2 book in price priority:

- Buy Market reads asks from lowest price to higher prices.
- Sell Market reads bids from highest price to lower prices.

Each consumed price level creates a deterministic `Fill`. Fill sequence IDs are simulator-local monotonic integers and reflect execution order.

## Historical Book Immutability

The authoritative historical `OrderBook` represents observed market data. Simulated execution reads visible liquidity from a snapshot returned by the const book interface, but it does not erase, reduce, or otherwise mutate historical book levels.

This is a zero-market-impact baseline:

```text
Simulated execution reads historical visible liquidity but does not mutate the authoritative historical OrderBook.
Market impact is not modeled.
```

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

## Phase 6 Limit-Order Policy

`OrderType::Limit` is represented structurally for future compatibility. Phase 6 does not implement marketable limit execution, resting orders, cancels, passive queue fills, or queue-ahead modeling.

Submitting a limit order to `ExecutionSimulator::execute_order` produces:

```text
New -> Pending -> Rejected
```

No fills are generated. This explicit rejection prevents accidental treatment of limits as market orders before Phase 8.

## Current Limitations

- No configurable latency or scheduled exchange-arrival event.
- No resting order book for simulated orders.
- No cancel request handling.
- No passive limit fills or L2 queue approximation.
- No portfolio, inventory, cash, PnL, fees-to-accounting, or equity logic.
- No market impact.
