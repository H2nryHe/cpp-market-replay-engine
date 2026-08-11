# Strategy Interface

Phase 5 adds an event-driven strategy interface on top of the deterministic Phase 4 event loop. It does not implement real orders, order IDs, acknowledgements, fills, execution simulation, latency-aware execution, portfolio accounting, PnL, benchmarking, Python bindings, or multithreading.

## Callback API

Strategies implement:

```cpp
on_book(const BookUpdateEvent&, const OrderBook&, IntentSink&)
on_trade(const TradeEvent&, const OrderBook&, IntentSink&)
on_timer(TimestampNs, const InternalEvent&, const OrderBook&, IntentSink&)
```

The `OrderBook` argument is `const`, so strategy code cannot directly mutate market state through the API. Market events are also passed by `const` reference. Strategies emit decisions through `IntentSink`; they do not call execution or accounting components.

## Callback Ordering

Callbacks follow the Phase 4 event-loop order exactly. Same-timestamp market events are not batched before strategy invocation.

For `BookUpdateEvent`:

1. the event loop processes the historical event;
2. the `OrderBook` applies the update;
3. `on_book` is invoked;
4. the strategy sees the post-update book state.

For `TradeEvent`:

1. source causal ordering is preserved;
2. no book mutation is invented;
3. `on_trade` is invoked with the current read-only book state.

Timer callbacks are invoked from Phase 4 `InternalEventType::Timer` events only. Generic future internal event categories do not imply order, cancel, fill, or execution behavior in this phase.

## OrderIntent

`OrderIntent` is a strategy-level decision object, not a real order. It contains:

- `side`;
- desired `quantity`;
- `order_type`;
- optional `limit_price_ticks`;
- `decision_timestamp_ns`.

It intentionally does not contain an order ID, exchange-arrival time, acknowledgement, status machine, filled quantity, cancellation lifecycle, or execution state.

## Queue Imbalance Demo

`QueueImbalanceStrategy` is a deterministic demonstration strategy used to exercise the event-driven strategy interface. It is not presented as a profitable trading strategy.

The signal is:

```text
QI = (bid_volume - ask_volume) / (bid_volume + ask_volume)
```

Visible volume is summed over configurable top-N book depth:

- bids: best bid toward worse bids;
- asks: best ask toward worse asks.

If `QI > buy_threshold`, the strategy emits one Buy intent. If `QI < sell_threshold`, it emits one Sell intent. Otherwise it emits no intent.

## Numeric Representation

Prices remain `PriceTicks`, and quantities remain `Quantity`. No price conversion or order-book key uses binary floating point.

The QI value itself is a derived dimensionless ratio and is computed as `double` after integer book volumes have been summed. This floating-point value is not stored in canonical market state and is not used as a price representation.

## Edge Policies

Conservative policies:

- if selected bid volume plus ask volume is zero, emit no intent;
- if either book side is empty, emit no intent;
- if the book is locked or crossed, emit no intent;
- duplicate signal behavior is Option A: evaluate on every `on_book` callback and emit at most one intent per callback when thresholds are crossed.

No cooldowns, transition filters, profitability logic, ML, training, transaction-cost tuning, or execution assumptions are implemented.

## Current Limitations

The strategy layer observes market events and emits intent records only. Intents do not affect replay state because execution does not exist yet.
