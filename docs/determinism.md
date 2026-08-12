# Determinism

The engine is designed so identical public inputs, configuration semantics, and engine version produce identical
functional outputs across clean environments.

## Event Ordering

Historical market events carry an `EventKey`:

```text
timestamp_ns
sequence_id
```

Ordering is deterministic:

1. lower `timestamp_ns` first;
2. for equal timestamps, lower `sequence_id` first;
3. in the event loop, historical market events at a timestamp are processed before same-timestamp internal events;
4. internal events at the same timestamp use deterministic insertion order.

This avoids assuming exchange timestamps are globally unique.

## Canonical State

Persistent hashes use deterministic FNV-1a over explicit canonical text. They do not use `std::hash`, memory addresses,
absolute paths, wall-clock timestamps, output directory names, local usernames, or build directories.

The public golden replay currently preserves:

```text
final_book_hash = 9ca1786003897355
run_hash        = 8aca37583ca6f83a
```

## Accounting Exactness

- Prices are integer `PriceTicks`.
- Quantities are integer normalized lots.
- Fees, cash, turnover, and PnL are integer accounting amounts.
- Half-tick midpoint marks are represented in doubled units, such as `mid_price_x2`.
- Accounting arithmetic uses checked intermediate calculations and fails explicitly on overflow.

## Regression Coverage

CTest covers component invariants, golden replay outputs, same-content different-path hashing, 100-run replay
determinism, CLI error paths, and Python/C++ binding equivalence when the optional binding is enabled.
