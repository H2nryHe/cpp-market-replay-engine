# Execution Model

Phase 6 added execution-domain orders, fills, market execution, and deterministic fees. Phase 7 integrated market-order execution with the deterministic event scheduler so strategy decisions become pending orders and execute only at exchange arrival time. Phase 8 adds marketable limit orders, resting simulated limit orders, deterministic cancels, partial passive fills, and an explicit L2 queue-depth approximation.

This phase does not implement portfolio accounting, PnL, exact FIFO/L3 queue reconstruction, exchange matching-engine emulation, market impact, hidden-liquidity modeling, stochastic fills, benchmark optimization, Python bindings, or multithreading.

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

At `OrderArrival`, execution reads the current historical `OrderBook` and then applies the market or limit arrival rules. Cancel requests also use the Phase 4 scheduler and arrive as `InternalEventType::CancelArrival`; there is no separate asynchronous cancellation system.

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

For partial market execution, `Canceled` means only the unfilled remainder was canceled after the arrival-time sweep. Existing fills remain valid. A market-order remainder never becomes a resting order.

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

Resting simulated limit orders are stored in a separate execution-layer active-order registry. They are not inserted into the historical bid or ask maps. Simulated fills and cancels do not reduce, delete, or otherwise rewrite historical levels.

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

The fee model uses one fixed-point rate represented in parts per million. Valid rates are in `[0, 1,000,000]`.

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

Maker/taker fee differentiation is not modeled in Phase 8; aggressive and passive fills use the same deterministic fee model.

## Limit-Order Arrival

Limit orders arrive through the same latency-aware scheduler as market orders.

Buy limit arrival:

- if `best_ask <= limit_price`, sweep asks from lowest upward;
- never execute above the limit price;
- any unfilled remainder rests as a simulated active buy limit at the limit price.

Sell limit arrival:

- if `best_bid >= limit_price`, sweep bids from highest downward;
- never execute below the limit price;
- any unfilled remainder rests as a simulated active sell limit at the limit price.

If no aggressive fill occurs, `Acknowledged` represents an active resting order. If an aggressive fill leaves a remainder, `PartiallyFilled` represents an active resting order. `Filled`, `Canceled`, and `Rejected` are terminal.

`ExecutionSimulator::execute_order` remains the Phase 6 direct market-order API; latency-aware limit behavior lives in `LatencyAwareExecution`.

## Active-Order Registry

The active registry stores simulated resting limit orders separately from historical book state. Each active record contains:

- `OrderId`;
- side;
- limit price;
- queue-ahead quantity;
- exchange-arrival timestamp.

The authoritative `Order` record retains original quantity, filled quantity, remaining quantity, status, timestamps, and limit price. Active orders are processed deterministically by registry order, which follows our own order ID / arrival order. This deterministic ordering is only among our simulated orders; it is not a claim about true exchange FIFO position against historical participants.

## Queue Fraction

Phase 8 represents queue fraction as fixed-point parts per million:

```text
queue_fraction_ppm in [0, 1,000,000]
250000  = 0.25
500000  = 0.50
750000  = 0.75
1000000 = 1.00
```

At order arrival, a newly resting order initializes:

```text
queue_ahead = round_half_up(visible_quantity_at_limit_price * queue_fraction_ppm / 1,000,000)
```

If no historical visible quantity exists at the limit price, `queue_ahead = 0`. This does not mean an immediate fill occurs; a qualifying future trade is still required.

For a marketable limit order that aggressively consumed visible quantity at its own limit price and still has a remainder, the remainder rests with `queue_ahead = 0` because the model already swept that visible limit-price quantity.

## Passive Fill Model

Passive fills from L2 data are queue-depth approximations rather than exact FIFO/L3 order reconstruction.

Only qualifying reported `TradeEvent` volume may reduce `queue_ahead` or generate passive fills. `BookUpdateEvent` size reductions, quote moves, level deletions, spread changes, and displayed quantity changes do not consume queue and do not fill simulated resting orders.

Exact-price qualifying trades:

- resting Buy limit at `P`: `aggressor_side == Sell` and `trade.price_ticks == P`;
- resting Sell limit at `P`: `aggressor_side == Buy` and `trade.price_ticks == P`.

Unknown aggressor side is conservative: no queue reduction and no passive fill.

Trade-through approximation:

- resting Buy at `P`: a Sell-aggressor trade below `P` sets queue ahead to zero and may fill at `P`;
- resting Sell at `P`: a Buy-aggressor trade above `P` sets queue ahead to zero and may fill at `P`.

Passive fill price is always the simulated order's limit price. Passive fill timestamp is the qualifying trade timestamp. No arbitrary price improvement is modeled.

For each historical trade, active simulated orders at the same side/price consume the trade quantity sequentially in deterministic order. The total passive fill quantity caused by that trade cannot exceed the trade's reported quantity after modeled queue-ahead consumption; reported trade volume is not duplicated across our own active orders.

## Cancellation

Cancel requests schedule `InternalEventType::CancelArrival` through the Phase 4 scheduler. Phase 8 uses zero cancel latency by default, with the same `TimestampNs`/`LatencyNs` checked arithmetic available when a cancel latency is supplied.

Only active resting limit orders can be canceled:

- `Acknowledged -> Canceled`;
- `PartiallyFilled -> Canceled`.

A canceled order keeps prior fills and filled quantity, is removed from active processing, and receives no future passive fills. Canceling `Filled`, `Canceled`, or `Rejected` orders fails explicitly.

Same-timestamp race semantics preserve Phase 4 ordering:

- `TradeEvent @ T` before `CancelArrival @ T`: the trade may fill the order before cancellation;
- `TradeEvent @ T` before `OrderArrival @ T`: the newly arriving order cannot receive passive fill from that earlier same-timestamp trade.

## Current Limitations

- L2 cannot identify exact FIFO position.
- Queue position is approximated from visible depth.
- Book-size reductions are not assumed to be executions.
- Unknown-aggressor trades do not trigger passive fills.
- Historical `OrderBook` remains zero-impact and immutable to simulated orders.
- Hidden and iceberg liquidity are not modeled.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Our simulated orders are ordered deterministically among themselves, but this does not imply true historical exchange FIFO position.
- No portfolio, inventory, cash, PnL, fees-to-accounting, or equity logic.
- No market impact.
