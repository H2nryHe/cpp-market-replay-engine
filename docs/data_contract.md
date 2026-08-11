# Data Contract

Phase 2 supports normalized historical market-data parsing only. It emits validated event records and does not implement an order book, replay loop, strategy, execution simulator, portfolio, or benchmark path.

## CSV Format

The first implementation uses simple comma-separated text without quoted fields. A header row is optional when it exactly matches the documented schema. Blank lines are ignored. Empty input or input containing only blank lines is valid and emits an empty feed.

## BookUpdate Schema

```text
timestamp_ns,sequence_id,side,price,quantity
```

- `timestamp_ns`: unsigned integer nanoseconds.
- `sequence_id`: unsigned integer secondary ordering key.
- `side`: one of `buy`, `sell`, `B`, or `S`.
- `price`: either a decimal price string converted through `price_to_ticks`, or an integer tick value when the parser is configured for tick input.
- `quantity`: non-negative integer `Quantity`. Zero is valid and represents a level-removal update for later order-book phases.

## Trade Schema

```text
timestamp_ns,sequence_id,price,quantity,aggressor_side
```

- `timestamp_ns`: unsigned integer nanoseconds.
- `sequence_id`: unsigned integer secondary ordering key.
- `price`: either a decimal price string converted through `price_to_ticks`, or an integer tick value when the parser is configured for tick input.
- `quantity`: non-negative integer `Quantity`.
- `aggressor_side`: optional. Empty means unknown. Non-empty values must be one of `buy`, `sell`, `B`, or `S`.

## Price And Quantity Rules

Decimal price fields remain strings until conversion through the Phase 1 deterministic `price_to_ticks` layer. The parser does not use `std::stod`, `float`, or `double` for prices. The rounding policy is no rounding: decimal prices that are not exact multiples of the configured tick size are rejected.

Quantities use the Phase 1 integer `Quantity` representation. The parser accepts non-negative integer text only.

## Source Ordering Policy

The feed preserves source-observed row order and never silently sorts events.

The accepted event-key order is strictly increasing by:

```text
timestamp_ns, then sequence_id
```

Policy:

- increasing timestamps are accepted;
- equal timestamps are accepted only when `sequence_id` increases;
- duplicate event keys are rejected;
- equal-timestamp rows with decreasing `sequence_id` are rejected;
- genuinely out-of-order input is rejected.

The same `sequence_id` may appear at different timestamps because `EventKey` is the `(timestamp_ns, sequence_id)` pair.

## Malformed Input

Malformed rows fail with `ParseError`. The error includes source, line number, field name, offending value, and reason where applicable. The parser rejects missing columns, malformed integers, invalid sides, invalid prices, invalid quantities, empty required fields, duplicate event keys, and out-of-order rows.
