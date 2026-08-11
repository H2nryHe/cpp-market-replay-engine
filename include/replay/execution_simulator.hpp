#pragma once

#include "replay/fill.hpp"
#include "replay/order.hpp"
#include "replay/order_book.hpp"
#include "replay/strategy.hpp"
#include "replay/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace replay {

class FeeModel {
 public:
  static constexpr std::int64_t denominator_ppm = 1'000'000;

  explicit FeeModel(std::int64_t fee_rate_ppm = 0);

  [[nodiscard]] std::int64_t fee_rate_ppm() const noexcept;
  [[nodiscard]] FeeAmount calculate_fee(PriceTicks price_ticks, Quantity quantity) const;

 private:
  std::int64_t fee_rate_ppm_{};
};

struct ExecutionConfig {
  FeeModel fee_model{};
};

class OrderFactory {
 public:
  Order create_order(Side side,
                     OrderType order_type,
                     Quantity quantity,
                     TimestampNs submit_timestamp_ns,
                     std::optional<PriceTicks> limit_price_ticks = std::nullopt);
  Order create_order_from_intent(const OrderIntent& intent);

 private:
  OrderId next_order_id_{1};
};

struct ExecutionResult {
  Order order;
  std::vector<Fill> fills{};
};

class ExecutionSimulator {
 public:
  explicit ExecutionSimulator(ExecutionConfig config = {});

  Order create_order_from_intent(const OrderIntent& intent);
  Order create_order(Side side,
                     OrderType order_type,
                     Quantity quantity,
                     TimestampNs submit_timestamp_ns,
                     std::optional<PriceTicks> limit_price_ticks = std::nullopt);

  ExecutionResult execute_order(Order order, const OrderBook& book);
  ExecutionResult execute_market_order(Order order, const OrderBook& book);

  [[nodiscard]] const ExecutionConfig& config() const noexcept;

 private:
  Fill make_fill(const Order& order, PriceTicks price_ticks, Quantity quantity);

  ExecutionConfig config_{};
  OrderFactory order_factory_{};
  FillSequenceId next_fill_sequence_id_{1};
};

}  // namespace replay
