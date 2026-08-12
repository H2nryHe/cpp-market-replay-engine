# Portfolio Accounting

Phase 9 adds deterministic portfolio accounting as an independent domain module. The portfolio is driven only by
`Fill` records. Order intent, submission, pending state, acknowledgement, cancellation, rejection, historical book
updates, and reported trades do not mutate cash, inventory, lots, fees, turnover, or PnL unless they first produce a
`Fill`.

## Units

- `PriceTicks` is the only price representation used by accounting.
- `Quantity` is signed only when exposed as net inventory. Individual open lots always store positive remaining
  quantity.
- `FeeAmount`, cash, realized gross PnL, turnover, and doubled mark values are fixed-width integer accounting amounts.
- Accounting calculations use checked `__int128` intermediates before explicit conversion back to the public integer
  amount type.
- Floating-point values are not canonical accounting state.

## Fill Validation

Applying a fill fails explicitly when:

- side is not `Buy` or `Sell`;
- price ticks are negative;
- quantity is negative or zero;
- fee amount is negative;
- the fill sequence id has already been applied;
- notional, cash, realized PnL, fee total, turnover, inventory, or mark arithmetic exceeds the accounting range.

Fill application is transactional: if validation or checked arithmetic fails, the portfolio state is left unchanged.

## Cash, Fees, Turnover, And Lots

Notional is `price_ticks * quantity`.

- Buy fill: cash decreases by `notional + fee`.
- Sell fill: cash increases by `notional - fee`.
- `total_fees` is the cumulative sum of fill fees.
- `turnover` is cumulative absolute traded notional.
- `fill_count` is the number of successfully applied fills.

Open position cost basis uses FIFO lots:

- A buy closes existing short lots from oldest to newest, then opens a long lot for any remainder.
- A sell closes existing long lots from oldest to newest, then opens a short lot for any remainder.
- Long inventory is positive. Short inventory is negative.
- Realized PnL is gross before fees.

For a long lot closed by a sell:

```text
realized_gross += (sell_price_ticks - entry_price_ticks) * matched_quantity
```

For a short lot closed by a buy:

```text
realized_gross += (entry_price_ticks - buy_price_ticks) * matched_quantity
```

## Mark-To-Market

Marking is explicit. The portfolio asks an `OrderBook` for a doubled midpoint:

```text
mid_price_x2 = best_bid_ticks + best_ask_ticks
```

This represents half ticks exactly. For example, bid `10000` and ask `10001` produce `mid_price_x2 = 20001`.

Mark availability:

- Empty, one-sided, or crossed books cannot mark nonzero inventory.
- Locked books are markable because bid equals ask and the doubled midpoint is exact.
- A flat portfolio can produce cash-only equity even when the book has no available mark.

For each open long lot:

```text
unrealized_gross_pnl_x2 += (mid_price_x2 - 2 * entry_price_ticks) * remaining_quantity
```

For each open short lot:

```text
unrealized_gross_pnl_x2 += (2 * entry_price_ticks - mid_price_x2) * remaining_quantity
```

Marked equity and net total PnL are:

```text
equity_x2 = 2 * cash + inventory * mid_price_x2
net_total_pnl_x2 = 2 * realized_gross_pnl + unrealized_gross_pnl_x2 - 2 * total_fees
```

For a successful mark, this identity must hold:

```text
equity_x2 - 2 * initial_cash == net_total_pnl_x2
```

## Non-Goals

The accounting module does not compute strategy performance optimization, Sharpe ratio, drawdown, risk limits, margin,
leverage, capital constraints, multi-asset allocation, benchmarks, or Python bindings.
