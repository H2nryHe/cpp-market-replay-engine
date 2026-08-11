# Order Book

Phase 3 implements deterministic visible L2 order-book reconstruction only. It consumes the normalized `BookUpdateEvent` records from Phase 2. It does not implement replay scheduling, strategies, orders, fills, execution, latency, portfolio accounting, benchmarking, Python bindings, or multithreading.

## Update Semantics

`BookUpdateEvent.quantity` is the resulting visible quantity at a price level, not an additive delta.

- `quantity > 0`: insert the level if absent, or replace the visible quantity if present.
- `quantity == 0`: remove the level if present.
- Deleting an absent level is a deterministic idempotent no-op.
- Negative prices or quantities are rejected by validation.

The implementation does not reinterpret updates as incremental changes.

## Data Structure

The baseline implementation uses standard ordered containers:

- bids: `std::map<PriceTicks, Quantity, std::greater<PriceTicks>>`, ordered highest price to lowest price;
- asks: `std::map<PriceTicks, Quantity, std::less<PriceTicks>>`, ordered lowest price to highest price.

No `double` or `float` price keys are used.

## Empty And One-Sided Behavior

Empty and one-sided books are valid states. `best_bid()` and `best_ask()` return empty optionals when the side is absent. `spread_ticks()` and `mid_price_x2_ticks()` also return empty optionals unless both sides are present.

`top_bids(0)` and `top_asks(0)` return empty vectors. If fewer than `N` levels exist, top-N snapshots return all available levels.

## Spread And Mid

Spread is represented exactly as:

```text
best_ask_ticks - best_bid_ticks
```

It can be negative for crossed books.

The midpoint is represented as twice the midpoint in ticks:

```text
mid_x2 = best_bid_ticks + best_ask_ticks
```

This preserves half-tick midpoints exactly. For example, bid `10000` and ask `10001` produce `mid_x2 = 20001`, representing `10000.5` ticks. If the sum overflows `std::int64_t`, `mid_price_x2_ticks()` throws `std::overflow_error`.

## Locked And Crossed Policy

The book uses a fidelity-preserving policy:

- source-observed updates are applied faithfully;
- locked and crossed states are not silently repaired;
- no levels are reordered, deleted, or altered to make the market valid.

Inspection methods expose the state:

- `is_locked()`: both sides exist and best bid equals best ask;
- `is_crossed()`: both sides exist and best bid is greater than best ask;
- `is_valid_two_sided_market()`: both sides exist and best bid is less than best ask.

Locked or crossed books may indicate transient feed state, missing initialization, incomplete data, or corrupt/source-specific behavior. This phase surfaces those states instead of hiding them.

## Canonical State Hash

`canonical_state()` serializes levels in explicit deterministic order:

```text
B,<price_ticks>,<quantity>\n
...
A,<price_ticks>,<quantity>\n
...
```

Bids are encoded best to worst, followed by asks best to worst. `state_hash()` applies FNV-1a 64-bit to the canonical bytes:

- offset basis: `14695981039346656037`;
- prime: `1099511628211`.

This hash is a deterministic regression checksum, not a cryptographic integrity claim.

## Current Limitations

The book reconstructs visible L2 levels only. It does not infer L3 order IDs, exact FIFO queue position, execution outcomes, strategy behavior, or portfolio accounting.
