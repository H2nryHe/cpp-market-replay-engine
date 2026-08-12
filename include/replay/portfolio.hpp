#pragma once

#include "replay/fill.hpp"
#include "replay/order_book.hpp"
#include "replay/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace replay {

using AccountingAmount = std::int64_t;
using AccountingAmountX2 = std::int64_t;

struct PositionLot {
  Side opening_side{};
  Quantity remaining_quantity{};
  PriceTicks entry_price_ticks{};

  friend constexpr bool operator==(const PositionLot&, const PositionLot&) = default;
};

struct AccountingLedgerEntry {
  FillSequenceId fill_sequence_id{};
  OrderId order_id{};
  TimestampNs fill_timestamp_ns{};
  Side side{};
  PriceTicks price_ticks{};
  Quantity quantity{};
  FeeAmount fee_amount{};
  AccountingAmount cash_after{};
  Quantity inventory_after{};
  AccountingAmount realized_gross_pnl_after{};

  friend constexpr bool operator==(const AccountingLedgerEntry&, const AccountingLedgerEntry&) = default;
};

struct PortfolioMark {
  std::optional<std::int64_t> mid_price_x2_ticks{};
  AccountingAmountX2 unrealized_gross_pnl_x2{};
  AccountingAmountX2 equity_x2{};
  AccountingAmountX2 net_total_pnl_x2{};

  friend constexpr bool operator==(const PortfolioMark&, const PortfolioMark&) = default;
};

class Portfolio {
 public:
  explicit Portfolio(AccountingAmount initial_cash = 0);

  void apply_fill(const Fill& fill);

  [[nodiscard]] AccountingAmount initial_cash() const noexcept;
  [[nodiscard]] AccountingAmount cash() const noexcept;
  [[nodiscard]] Quantity inventory() const;
  [[nodiscard]] AccountingAmount realized_gross_pnl() const noexcept;
  [[nodiscard]] FeeAmount total_fees() const noexcept;
  [[nodiscard]] AccountingAmount turnover() const noexcept;
  [[nodiscard]] std::size_t fill_count() const noexcept;
  [[nodiscard]] const std::vector<PositionLot>& open_lots() const noexcept;
  [[nodiscard]] const std::vector<AccountingLedgerEntry>& ledger() const noexcept;

  [[nodiscard]] std::optional<PortfolioMark> mark_to_market(const OrderBook& book) const;
  [[nodiscard]] PortfolioMark cash_only_mark() const;

 private:
  void reject_duplicate_fill(FillSequenceId fill_sequence_id) const;
  void apply_buy_lot(const Fill& fill);
  void apply_sell_lot(const Fill& fill);
  void record_ledger_entry(const Fill& fill);

  AccountingAmount initial_cash_{};
  AccountingAmount cash_{};
  AccountingAmount realized_gross_pnl_{};
  FeeAmount total_fees_{};
  AccountingAmount turnover_{};
  std::vector<PositionLot> lots_;
  std::vector<FillSequenceId> applied_fill_sequence_ids_;
  std::vector<AccountingLedgerEntry> ledger_;
};

AccountingAmount checked_notional_amount(PriceTicks price_ticks, Quantity quantity);
AccountingAmount checked_accounting_add(AccountingAmount lhs, AccountingAmount rhs, const char* field_name);
AccountingAmount checked_accounting_sub(AccountingAmount lhs, AccountingAmount rhs, const char* field_name);
AccountingAmountX2 checked_accounting_x2(AccountingAmount value, const char* field_name);

std::string canonical_lot_string(const PositionLot& lot);

}  // namespace replay
