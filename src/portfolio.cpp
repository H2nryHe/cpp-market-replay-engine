#include "replay/portfolio.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace replay {
namespace {

AccountingAmount checked_to_accounting_amount(__int128 value, const char* field_name) {
  if (value > static_cast<__int128>(std::numeric_limits<AccountingAmount>::max()) ||
      value < static_cast<__int128>(std::numeric_limits<AccountingAmount>::min())) {
    throw std::overflow_error(std::string{field_name} + " exceeds accounting range");
  }
  return static_cast<AccountingAmount>(value);
}

AccountingAmountX2 checked_to_accounting_x2(__int128 value, const char* field_name) {
  if (value > static_cast<__int128>(std::numeric_limits<AccountingAmountX2>::max()) ||
      value < static_cast<__int128>(std::numeric_limits<AccountingAmountX2>::min())) {
    throw std::overflow_error(std::string{field_name} + " exceeds doubled accounting range");
  }
  return static_cast<AccountingAmountX2>(value);
}

void validate_fill_for_accounting(const Fill& fill) {
  if (fill.side != Side::Buy && fill.side != Side::Sell) {
    throw std::invalid_argument("fill side must be buy or sell");
  }
  validate_price_ticks(fill.price_ticks);
  validate_quantity(fill.quantity);
  if (fill.quantity == 0) {
    throw std::invalid_argument("fill quantity must be greater than zero");
  }
  if (fill.fee_amount < 0) {
    throw std::invalid_argument("fill fee_amount must be non-negative");
  }
}

bool is_long(const PositionLot& lot) noexcept {
  return lot.opening_side == Side::Buy;
}

AccountingAmount matched_realized_pnl(const PositionLot& lot, PriceTicks exit_price_ticks, Quantity matched_quantity) {
  const auto price_delta = is_long(lot) ? static_cast<__int128>(exit_price_ticks) - lot.entry_price_ticks
                                       : static_cast<__int128>(lot.entry_price_ticks) - exit_price_ticks;
  return checked_to_accounting_amount(price_delta * static_cast<__int128>(matched_quantity), "realized_gross_pnl");
}

AccountingAmountX2 lot_unrealized_x2(const PositionLot& lot, std::int64_t mid_price_x2_ticks) {
  const auto entry_x2 = static_cast<__int128>(lot.entry_price_ticks) * 2;
  const auto delta_x2 = is_long(lot) ? static_cast<__int128>(mid_price_x2_ticks) - entry_x2
                                    : entry_x2 - static_cast<__int128>(mid_price_x2_ticks);
  return checked_to_accounting_x2(delta_x2 * static_cast<__int128>(lot.remaining_quantity), "unrealized_gross_pnl_x2");
}

std::optional<std::int64_t> mark_mid_x2(const OrderBook& book) {
  if (book.empty() || !book.best_bid().has_value() || !book.best_ask().has_value()) {
    return std::nullopt;
  }
  if (book.is_crossed()) {
    return std::nullopt;
  }
  return book.mid_price_x2_ticks();
}

}  // namespace

Portfolio::Portfolio(AccountingAmount initial_cash) : initial_cash_{initial_cash}, cash_{initial_cash} {}

void Portfolio::apply_fill(const Fill& fill) {
  validate_fill_for_accounting(fill);
  reject_duplicate_fill(fill.fill_sequence_id);

  Portfolio next = *this;
  const auto notional = checked_notional_amount(fill.price_ticks, fill.quantity);
  if (fill.side == Side::Buy) {
    next.cash_ = checked_accounting_sub(next.cash_, notional, "cash");
    next.cash_ = checked_accounting_sub(next.cash_, fill.fee_amount, "cash");
    next.apply_buy_lot(fill);
  } else {
    next.cash_ = checked_accounting_add(next.cash_, notional, "cash");
    next.cash_ = checked_accounting_sub(next.cash_, fill.fee_amount, "cash");
    next.apply_sell_lot(fill);
  }

  next.total_fees_ = checked_accounting_add(next.total_fees_, fill.fee_amount, "total_fees");
  next.turnover_ = checked_accounting_add(next.turnover_, notional, "turnover");
  next.applied_fill_sequence_ids_.push_back(fill.fill_sequence_id);
  next.record_ledger_entry(fill);
  *this = std::move(next);
}

AccountingAmount Portfolio::initial_cash() const noexcept {
  return initial_cash_;
}

AccountingAmount Portfolio::cash() const noexcept {
  return cash_;
}

Quantity Portfolio::inventory() const {
  __int128 total = 0;
  for (const auto& lot : lots_) {
    total += (lot.opening_side == Side::Buy ? 1 : -1) * static_cast<__int128>(lot.remaining_quantity);
  }
  if (total > static_cast<__int128>(std::numeric_limits<Quantity>::max()) ||
      total < static_cast<__int128>(std::numeric_limits<Quantity>::min())) {
    throw std::overflow_error("inventory exceeds Quantity range");
  }
  return static_cast<Quantity>(total);
}

AccountingAmount Portfolio::realized_gross_pnl() const noexcept {
  return realized_gross_pnl_;
}

FeeAmount Portfolio::total_fees() const noexcept {
  return total_fees_;
}

AccountingAmount Portfolio::turnover() const noexcept {
  return turnover_;
}

std::size_t Portfolio::fill_count() const noexcept {
  return applied_fill_sequence_ids_.size();
}

const std::vector<PositionLot>& Portfolio::open_lots() const noexcept {
  return lots_;
}

const std::vector<AccountingLedgerEntry>& Portfolio::ledger() const noexcept {
  return ledger_;
}

std::optional<PortfolioMark> Portfolio::mark_to_market(const OrderBook& book) const {
  const auto current_inventory = inventory();
  const auto maybe_mid_x2 = mark_mid_x2(book);
  if (current_inventory != 0 && !maybe_mid_x2.has_value()) {
    return std::nullopt;
  }

  AccountingAmountX2 unrealized_x2 = 0;
  if (maybe_mid_x2.has_value()) {
    for (const auto& lot : lots_) {
      unrealized_x2 =
          checked_to_accounting_x2(static_cast<__int128>(unrealized_x2) + lot_unrealized_x2(lot, *maybe_mid_x2),
                                   "unrealized_gross_pnl_x2");
    }
  }

  const auto cash_x2 = checked_accounting_x2(cash_, "equity_x2");
  const auto marked_inventory =
      maybe_mid_x2.has_value()
          ? checked_to_accounting_x2(static_cast<__int128>(current_inventory) * static_cast<__int128>(*maybe_mid_x2),
                                     "marked_inventory_x2")
          : AccountingAmountX2{0};
  const auto equity_x2 =
      checked_to_accounting_x2(static_cast<__int128>(cash_x2) + marked_inventory, "equity_x2");
  const auto net_total_pnl_x2 = checked_to_accounting_x2((static_cast<__int128>(realized_gross_pnl_) * 2) +
                                                             unrealized_x2 -
                                                             (static_cast<__int128>(total_fees_) * 2),
                                                         "net_total_pnl_x2");

  return PortfolioMark{maybe_mid_x2, unrealized_x2, equity_x2, net_total_pnl_x2};
}

PortfolioMark Portfolio::cash_only_mark() const {
  if (inventory() != 0) {
    throw std::logic_error("cash-only mark requires zero inventory");
  }
  const auto cash_x2 = checked_accounting_x2(cash_, "equity_x2");
  const auto net_total_pnl_x2 =
      checked_to_accounting_x2((static_cast<__int128>(realized_gross_pnl_) * 2) -
                                   (static_cast<__int128>(total_fees_) * 2),
                               "net_total_pnl_x2");
  return PortfolioMark{std::nullopt, 0, cash_x2, net_total_pnl_x2};
}

void Portfolio::reject_duplicate_fill(FillSequenceId fill_sequence_id) const {
  if (std::find(applied_fill_sequence_ids_.begin(), applied_fill_sequence_ids_.end(), fill_sequence_id) !=
      applied_fill_sequence_ids_.end()) {
    throw std::invalid_argument("duplicate fill_sequence_id");
  }
}

void Portfolio::apply_buy_lot(const Fill& fill) {
  Quantity remaining = fill.quantity;
  while (remaining > 0 && !lots_.empty() && lots_.front().opening_side == Side::Sell) {
    auto& lot = lots_.front();
    const auto matched_quantity = std::min(remaining, lot.remaining_quantity);
    realized_gross_pnl_ = checked_accounting_add(
        realized_gross_pnl_, matched_realized_pnl(lot, fill.price_ticks, matched_quantity), "realized_gross_pnl");
    lot.remaining_quantity -= matched_quantity;
    remaining -= matched_quantity;
    if (lot.remaining_quantity == 0) {
      lots_.erase(lots_.begin());
    }
  }
  if (remaining > 0) {
    lots_.push_back(PositionLot{Side::Buy, remaining, fill.price_ticks});
  }
}

void Portfolio::apply_sell_lot(const Fill& fill) {
  Quantity remaining = fill.quantity;
  while (remaining > 0 && !lots_.empty() && lots_.front().opening_side == Side::Buy) {
    auto& lot = lots_.front();
    const auto matched_quantity = std::min(remaining, lot.remaining_quantity);
    realized_gross_pnl_ = checked_accounting_add(
        realized_gross_pnl_, matched_realized_pnl(lot, fill.price_ticks, matched_quantity), "realized_gross_pnl");
    lot.remaining_quantity -= matched_quantity;
    remaining -= matched_quantity;
    if (lot.remaining_quantity == 0) {
      lots_.erase(lots_.begin());
    }
  }
  if (remaining > 0) {
    lots_.push_back(PositionLot{Side::Sell, remaining, fill.price_ticks});
  }
}

void Portfolio::record_ledger_entry(const Fill& fill) {
  ledger_.push_back(AccountingLedgerEntry{.fill_sequence_id = fill.fill_sequence_id,
                                         .order_id = fill.order_id,
                                         .fill_timestamp_ns = fill.fill_timestamp_ns,
                                         .side = fill.side,
                                         .price_ticks = fill.price_ticks,
                                         .quantity = fill.quantity,
                                         .fee_amount = fill.fee_amount,
                                         .cash_after = cash_,
                                         .inventory_after = inventory(),
                                         .realized_gross_pnl_after = realized_gross_pnl_});
}

AccountingAmount checked_notional_amount(PriceTicks price_ticks, Quantity quantity) {
  validate_price_ticks(price_ticks);
  validate_quantity(quantity);
  const auto notional = static_cast<__int128>(price_ticks) * static_cast<__int128>(quantity);
  return checked_to_accounting_amount(notional, "notional");
}

AccountingAmount checked_accounting_add(AccountingAmount lhs, AccountingAmount rhs, const char* field_name) {
  return checked_to_accounting_amount(static_cast<__int128>(lhs) + static_cast<__int128>(rhs), field_name);
}

AccountingAmount checked_accounting_sub(AccountingAmount lhs, AccountingAmount rhs, const char* field_name) {
  return checked_to_accounting_amount(static_cast<__int128>(lhs) - static_cast<__int128>(rhs), field_name);
}

AccountingAmountX2 checked_accounting_x2(AccountingAmount value, const char* field_name) {
  return checked_to_accounting_x2(static_cast<__int128>(value) * 2, field_name);
}

std::string canonical_lot_string(const PositionLot& lot) {
  std::ostringstream os;
  os << (lot.opening_side == Side::Buy ? "long" : "short") << ',' << lot.remaining_quantity << ','
     << lot.entry_price_ticks;
  return os.str();
}

}  // namespace replay
