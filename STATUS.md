# Project Status

## Current phase
Phase 9 - Portfolio, PnL & Accounting

## Phase status
PASS

## Last verified commit
19cbcc4

## Build
- Debug: PASS
- Release: PASS
- ASan/UBSan: PASS

## Tests
- CTest: 10/10 passed
- CLI smoke: PASS
- Phase 9 portfolio/accounting tests: PASS
- Determinism: PASS for fixed fill accounting sequence repeated 100 times

## Benchmarks
Not started

## Completed phases
- Phase 0 - PASS
- Phase 1 - PASS
- Phase 2 - PASS
- Phase 3 - PASS
- Phase 4 - PASS
- Phase 5 - PASS
- Phase 6 - PASS
- Phase 7 - PASS
- Phase 8 - PASS
- Phase 9 - PASS

## Implementation summary
- Added an independent `Portfolio` domain module driven only by `Fill` records.
- Added deterministic FIFO open-lot accounting with long, short, partial close, full close, and position-flip support.
- Added cash, signed inventory, open lots, realized gross PnL, gross unrealized PnL at mark, total fees, equity, net total PnL, turnover, fill count, and a simple deterministic fill ledger.
- Added explicit mark-to-market using `OrderBook` doubled midpoint values.
- Added checked integer accounting helpers using `__int128` intermediates and explicit checked conversion.
- Added transactional fill application: invalid or overflowing fills fail explicitly and leave portfolio state unchanged.
- Added `portfolio_tests` covering Phase 9 required cases A-AD.
- Added `docs/accounting.md` documenting accounting units, FIFO behavior, mark policy, identities, overflow handling, duplicate fill policy, and limitations.

## Files changed
- `CMakeLists.txt`
- `include/replay/portfolio.hpp`
- `src/portfolio.cpp`
- `tests/unit/portfolio_test.cpp`
- `docs/accounting.md`
- `STATUS.md`

## Accounting numeric units
- Prices use existing `PriceTicks`.
- Quantities use existing `Quantity`.
- Fees use existing `FeeAmount` from `Fill`.
- Cash, realized gross PnL, turnover, and notional use `AccountingAmount`, a signed 64-bit integer in canonical `PriceTicks * Quantity` units.
- Marked values that may contain half ticks use `AccountingAmountX2`, a signed 64-bit doubled accounting unit.
- No `double` or `float` is used for canonical portfolio accounting state.

## Overflow strategy
- Products such as `price_ticks * quantity`, doubled cash, inventory times doubled midpoint, realized gross PnL, unrealized gross PnL, fees, and turnover are calculated with `__int128` intermediates.
- Conversion back to `AccountingAmount` or `AccountingAmountX2` is checked.
- Overflow throws explicitly; no signed or unsigned wraparound is accepted.
- Fill application works on a copy and commits only after validation and checked arithmetic complete.

## Cash convention
- Buy fill: `cash -= price_ticks * quantity`, then `cash -= fee`.
- Sell fill: `cash += price_ticks * quantity`, then `cash -= fee`.
- Starting cash is configurable; zero starting cash is allowed.

## Inventory convention
- Long inventory is positive.
- Short inventory is negative.
- Flat inventory is zero.
- Aggregate signed inventory is derived from open lots.

## Lot-accounting method
- FIFO lot accounting is authoritative for position cost basis.
- Buy fills close existing short lots FIFO, then open a long lot for any remainder.
- Sell fills close existing long lots FIFO, then open a short lot for any remainder.
- Partial closes, complete closes, long-to-short flips, and short-to-long flips are tested.

## Fee policy
- Portfolio consumes the deterministic `fee_amount` already present on each `Fill`.
- Portfolio does not recalculate fees with a separate model.
- `total_fees` is tracked separately from gross realized and gross unrealized PnL.

## Realized PnL definition
- Realized gross PnL is before fees.
- Long close: `(exit_sell_price_ticks - entry_buy_price_ticks) * matched_quantity`.
- Short close: `(entry_sell_price_ticks - exit_buy_price_ticks) * matched_quantity`.

## Unrealized PnL definition
- Gross unrealized PnL is derived from open lots and the current explicit market mark.
- Long lot: `(mid_price_x2 - 2 * entry_price_ticks) * remaining_quantity`.
- Short lot: `(2 * entry_price_ticks - mid_price_x2) * remaining_quantity`.
- Values are represented exactly in doubled accounting units.

## Mark policy
- Marking uses the Phase 3 `OrderBook` best bid and best ask.
- Empty and one-sided books do not provide a two-sided mark for nonzero inventory.
- Locked books are markable.
- Crossed books are treated conservatively as mark unavailable for nonzero inventory.
- Flat portfolios can return cash-only equity even if the book mark is unavailable.

## Half-tick representation
- Midpoint is represented as `mid_price_x2 = best_bid_ticks + best_ask_ticks`.
- Equity is represented as `equity_x2 = 2 * cash + inventory * mid_price_x2`.
- Half-tick midpoint values are not truncated.

## Turnover definition
- Turnover is cumulative absolute traded notional.
- For each fill: `turnover += price_ticks * quantity`.
- Turnover is not divided by capital or equity in Phase 9.

## Duplicate Fill policy
- Portfolio records applied `fill_sequence_id` values.
- Reapplying the same fill sequence id throws `std::invalid_argument`.
- Duplicate fills do not mutate cash, inventory, lots, fees, realized gross PnL, turnover, fill count, or ledger state.

## Accounting invariants
- Only successfully applied `Fill` records mutate cash, inventory, lots, realized gross PnL, fees, turnover, fill count, and ledger state.
- `OrderIntent`, order submission, pending state, acknowledgement, cancellation, rejection, `BookUpdateEvent`, and `TradeEvent` do not directly mutate portfolio economics.
- For available marks: `equity_x2 == 2 * cash + inventory * mid_price_x2`.
- When there are no external cash flows: `equity_x2 - 2 * initial_cash == net_total_pnl_x2`.
- `net_total_pnl_x2 == 2 * realized_gross_pnl + unrealized_gross_pnl_x2 - 2 * total_fees`.

## Deterministic result
- A fixed fill sequence repeated 100 times produced identical cash, inventory, lots, realized gross PnL, fees, turnover, equity marks, and net total PnL marks.
- Accounting depends on economic fill data, not on aggressive versus passive matching origin.

## Privacy/Git hygiene
- `PROJECT_SPEC.md` remains local-only and ignored by `.gitignore`.
- No private/local-only files are staged.
- No absolute local-machine paths were found in public files.
- No fixtures were added in Phase 9.

## Known limitations
- `cmake` was not initially installed and was installed with Homebrew during Phase 0 verification.
- CSV support is intentionally simple: comma-separated fields without quoted-field handling.
- L2 data cannot identify exact FIFO position, exact queue composition, hidden or iceberg liquidity, or cancellations ahead of our simulated order.
- Exchange-specific matching rules are not modeled.
- Maker/taker fee differentiation is not modeled.
- Accounting is single-instrument and consumes existing `Fill` records only.
- Portfolio does not enforce margin, leverage, capital constraints, risk limits, or multi-asset allocation.
- Strategy performance statistics such as Sharpe, Sortino, volatility, alpha, beta, drawdown, win rate, and return series are not implemented.
- Market impact, benchmarking, Python bindings, multithreading, and performance optimization are not implemented.
- Phase 10 end-to-end CLI/artifact pipeline has not been started.

## Next phase
Phase 10 - End-to-End Replay CLI, Reports & Artifacts

## Verification commands
```bash
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
Result: PASS - Debug build configured.

$ cmake --build build
Result: PASS - built market_replay, replay_cli, smoke_tests, domain_types_tests, market_feed_tests, order_book_tests, event_loop_tests, strategy_tests, execution_tests, latency_execution_tests, passive_limit_tests, and portfolio_tests.

$ ctest --test-dir build --output-on-failure
Result: PASS - 10/10 tests passed.

$ ./build/replay_cli --help
Result: PASS - exited 0 and printed usage.

$ cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
Result: PASS - sanitizer build configured.

$ cmake --build build-asan
Result: PASS - built sanitizer targets.

$ ctest --test-dir build-asan --output-on-failure
Result: PASS - 10/10 tests passed under ASan/UBSan configuration.

$ cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
Result: PASS - Release build configured.

$ cmake --build build-release
Result: PASS - built Release targets.

$ ./build/portfolio_tests
Result: PASS - Phase 9 portfolio/accounting tests passed.

$ git diff --check
Result: PASS - no whitespace errors.

$ rg "\b(double|float)\b" include/replay/portfolio.hpp src/portfolio.cpp tests/unit/portfolio_test.cpp docs/accounting.md
Result: PASS - no floating-point canonical accounting state or tests found.

$ rg "Sortino|volatility|alpha|beta|drawdown|win rate|return series|Sharpe|risk limits|margin|leverage|capital constraints|multi-asset|benchmark optimization|Python bindings|multithreading" include/replay/portfolio.hpp src/portfolio.cpp tests/unit/portfolio_test.cpp docs/accounting.md
Result: PASS - matches only the documented non-goals in docs/accounting.md.

$ rg "$(printf '\057Users\057\174\057private\057\174Local\040Documents')" -g '!PROJECT_SPEC.md' -g '!build/**' -g '!build-asan/**' -g '!build-release/**' -g '!.git/**'
Result: PASS - no absolute local-machine paths found in public files.

$ git check-ignore -v PROJECT_SPEC.md
Result: PASS - `PROJECT_SPEC.md` is ignored by `.gitignore`.

$ git status --ignored --short
Result: PASS - only Phase 9 source/docs/CMake/status files are modified or untracked; `PROJECT_SPEC.md` and build directories are ignored.
```

## Phase 0 acceptance gate
- clean configure: PASS
- clean build: PASS
- CTest passes: PASS
- CLI `--help` works: PASS
- sanitizer build works where supported: PASS
- `STATUS.md` records exact commands/results: PASS

## Phase 1 acceptance gate
- no market price key uses `double`: PASS
- event ordering has deterministic tests: PASS
- conversion rules documented: PASS
- all unit tests pass: PASS
- sanitizer tests pass: PASS

## Phase 2 acceptance gate
- parser handles valid fixture: PASS
- parser rejects malformed fixture: PASS
- deterministic ordering confirmed: PASS
- errors are actionable: PASS
- no premature optimization: PASS

## Phase 3 acceptance gate
- all book operations correct: PASS
- edge cases tested: PASS
- golden replay passes: PASS
- deterministic state hash stable: PASS
- no negative quantities: PASS
- sanitizer tests pass: PASS

## Phase 4 acceptance gate
- event loop is single-threaded: PASS
- tie-breaking documented: PASS
- internal events interleave correctly with market events: PASS
- deterministic trace test passes: PASS
- no later phase logic embedded prematurely: PASS

## Phase 5 acceptance gate
- strategy interface cleanly separated: PASS
- QI demo works: PASS
- no direct order-book mutation: PASS
- no future-event access: PASS
- strategy behavior unit tested: PASS

## Phase 6 acceptance gate
- market orders walk depth correctly: PASS
- partial execution supported: PASS
- fees deterministic: PASS
- lifecycle transition table documented: PASS
- fill invariants tested: PASS

## Phase 7 acceptance gate
- no execution uses stale decision-time book by accident: PASS
- intervening events processed correctly: PASS
- latency configuration works: PASS

## Phase 8 acceptance gate
- limit orders, cancel, partial fill, and passive queue approximation implemented: PASS
- active simulated orders do not mutate historical `OrderBook`: PASS
- same-timestamp race semantics preserved: PASS
- passive fill conservation tested: PASS
- all Phase 0-7 behavior preserved: PASS

## Phase 9 acceptance gate
- portfolio state mutates only from `Fill` records: PASS
- cash, signed inventory, FIFO lots, realized gross PnL, gross unrealized PnL, total fees, equity, turnover, and fill count tracked: PASS
- no `double`/`float` canonical accounting state: PASS
- checked arithmetic with `__int128` intermediates implemented and tested: PASS
- FIFO examples, partial closes, and position flips tested: PASS
- fees remain separate from gross realized and gross unrealized PnL: PASS
- half-tick mark representation tested: PASS
- equity and net total PnL identities tested: PASS
- empty, one-sided, locked, crossed, and flat mark policies tested: PASS
- zero quantity, invalid values, duplicate fills, and overflow failures tested: PASS
- execution-produced fills feed portfolio accounting: PASS
- all Phase 0-8 tests continue to pass: PASS
- Phase 10 not started: PASS
