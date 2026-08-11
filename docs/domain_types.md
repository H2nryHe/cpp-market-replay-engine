# Domain Types

Phase 1 defines only reusable primitives. It does not implement market-feed parsing, order-book storage, replay scheduling, strategies, execution, or portfolio accounting.

## Integer Units

- `TimestampNs` is an unsigned integer nanosecond timestamp.
- `PriceTicks` is a signed integer tick count. Validation rejects negative price ticks.
- `Quantity` is a signed integer normalized lot count. Validation rejects negative quantities. Zero is valid because later L2 book updates use zero to remove a visible level.
- `LatencyNs` stores non-negative nanoseconds. Construction from negative nanoseconds or microseconds throws.

## Price Conversion

Decimal price conversion is deterministic and exact:

- callers pass decimal prices and tick sizes as plain decimal strings, such as `100.25` and `0.01`;
- binary floating-point inputs are intentionally not accepted by the conversion API;
- tick size must be greater than zero;
- decimal prices must be non-negative;
- the rounding policy is no rounding: a price that is not an exact multiple of the tick size throws;
- examples with `tick_size = 0.01`: `100.25 -> 10025` and `100.00 -> 10000`;
- formatting ticks back to a decimal string preserves the normalized tick-size precision, so `10000` ticks at `0.01` formats as `100.00`.

## Enum Parsing

Supported `Side` aliases are exactly:

- `buy`
- `sell`
- `B`
- `S`

Supported order types are exactly `market` and `limit`. Supported order statuses are `new`, `pending`, `acknowledged`, `partially_filled`, `filled`, `canceled`, and `rejected`. Other spellings fail explicitly.

## Event Ordering

`EventKey` orders events first by `timestamp_ns` and then by `sequence_id`. This gives deterministic ordering for events with identical timestamps and avoids assuming exchange timestamps are globally unique.
