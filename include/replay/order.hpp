#pragma once

#include "replay/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace replay {

class Order {
 public:
  Order(OrderId order_id,
        Side side,
        OrderType order_type,
        Quantity original_quantity,
        TimestampNs submit_timestamp_ns,
        TimestampNs exchange_arrival_timestamp_ns,
        std::optional<PriceTicks> limit_price_ticks = std::nullopt);

  [[nodiscard]] OrderId order_id() const noexcept;
  [[nodiscard]] Side side() const noexcept;
  [[nodiscard]] OrderType order_type() const noexcept;
  [[nodiscard]] Quantity original_quantity() const noexcept;
  [[nodiscard]] Quantity filled_quantity() const noexcept;
  [[nodiscard]] Quantity remaining_quantity() const noexcept;
  [[nodiscard]] TimestampNs submit_timestamp_ns() const noexcept;
  [[nodiscard]] TimestampNs exchange_arrival_timestamp_ns() const noexcept;
  [[nodiscard]] std::optional<PriceTicks> limit_price_ticks() const noexcept;
  [[nodiscard]] OrderStatus status() const noexcept;
  [[nodiscard]] const std::vector<OrderStatus>& status_history() const noexcept;

  void transition_to(OrderStatus next_status);
  void record_fill_quantity(Quantity fill_quantity);

 private:
  OrderId order_id_{};
  Side side_{};
  OrderType order_type_{OrderType::Market};
  Quantity original_quantity_{};
  Quantity filled_quantity_{};
  TimestampNs submit_timestamp_ns_{};
  TimestampNs exchange_arrival_timestamp_ns_{};
  std::optional<PriceTicks> limit_price_ticks_{};
  OrderStatus status_{OrderStatus::New};
  std::vector<OrderStatus> status_history_{OrderStatus::New};
};

bool is_valid_order_transition(OrderStatus current_status, OrderStatus next_status) noexcept;
std::string order_status_name(OrderStatus status);
std::string canonical_order_string(const Order& order);

}  // namespace replay
