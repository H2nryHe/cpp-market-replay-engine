#include "replay/order.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>

namespace replay {
namespace {

void validate_positive_quantity(Quantity quantity, const char* field_name) {
  validate_quantity(quantity);
  if (quantity == 0) {
    throw std::invalid_argument(std::string{field_name} + " must be greater than zero");
  }
}

}  // namespace

Order::Order(OrderId order_id,
             Side side,
             OrderType order_type,
             Quantity original_quantity,
             TimestampNs submit_timestamp_ns,
             TimestampNs exchange_arrival_timestamp_ns,
             std::optional<PriceTicks> limit_price_ticks)
    : order_id_{order_id},
      side_{side},
      order_type_{order_type},
      original_quantity_{original_quantity},
      decision_timestamp_ns_{submit_timestamp_ns},
      submit_timestamp_ns_{submit_timestamp_ns},
      exchange_arrival_timestamp_ns_{exchange_arrival_timestamp_ns},
      limit_price_ticks_{limit_price_ticks} {
  if (order_id_ == 0) {
    throw std::invalid_argument("order_id must be greater than zero");
  }
  validate_positive_quantity(original_quantity_, "original_quantity");
  if (limit_price_ticks_.has_value()) {
    validate_price_ticks(*limit_price_ticks_);
  }
  if (order_type_ == OrderType::Limit && !limit_price_ticks_.has_value()) {
    throw std::invalid_argument("limit order requires limit_price_ticks");
  }
}

OrderId Order::order_id() const noexcept {
  return order_id_;
}

Side Order::side() const noexcept {
  return side_;
}

OrderType Order::order_type() const noexcept {
  return order_type_;
}

Quantity Order::original_quantity() const noexcept {
  return original_quantity_;
}

Quantity Order::filled_quantity() const noexcept {
  return filled_quantity_;
}

Quantity Order::remaining_quantity() const noexcept {
  return original_quantity_ - filled_quantity_;
}

TimestampNs Order::decision_timestamp_ns() const noexcept {
  return decision_timestamp_ns_;
}

TimestampNs Order::submit_timestamp_ns() const noexcept {
  return submit_timestamp_ns_;
}

TimestampNs Order::exchange_arrival_timestamp_ns() const noexcept {
  return exchange_arrival_timestamp_ns_;
}

std::optional<PriceTicks> Order::limit_price_ticks() const noexcept {
  return limit_price_ticks_;
}

OrderStatus Order::status() const noexcept {
  return status_;
}

const std::vector<OrderStatus>& Order::status_history() const noexcept {
  return status_history_;
}

void Order::transition_to(OrderStatus next_status) {
  if (!is_valid_order_transition(status_, next_status)) {
    throw std::invalid_argument("invalid order status transition: " + order_status_name(status_) + " -> " +
                                order_status_name(next_status));
  }
  status_ = next_status;
  status_history_.push_back(next_status);
}

void Order::record_fill_quantity(Quantity fill_quantity) {
  validate_positive_quantity(fill_quantity, "fill_quantity");
  if (fill_quantity > remaining_quantity()) {
    throw std::invalid_argument("fill_quantity exceeds order remaining quantity");
  }
  filled_quantity_ += fill_quantity;
}

bool is_valid_order_transition(OrderStatus current_status, OrderStatus next_status) noexcept {
  switch (current_status) {
    case OrderStatus::New:
      return next_status == OrderStatus::Pending;
    case OrderStatus::Pending:
      return next_status == OrderStatus::Acknowledged || next_status == OrderStatus::Rejected;
    case OrderStatus::Acknowledged:
      return next_status == OrderStatus::PartiallyFilled || next_status == OrderStatus::Filled ||
             next_status == OrderStatus::Canceled;
    case OrderStatus::PartiallyFilled:
      return next_status == OrderStatus::Filled || next_status == OrderStatus::Canceled;
    case OrderStatus::Filled:
    case OrderStatus::Canceled:
    case OrderStatus::Rejected:
      return false;
  }
  return false;
}

std::string order_status_name(OrderStatus status) {
  switch (status) {
    case OrderStatus::New:
      return "new";
    case OrderStatus::Pending:
      return "pending";
    case OrderStatus::Acknowledged:
      return "acknowledged";
    case OrderStatus::PartiallyFilled:
      return "partially_filled";
    case OrderStatus::Filled:
      return "filled";
    case OrderStatus::Canceled:
      return "canceled";
    case OrderStatus::Rejected:
      return "rejected";
  }
  throw std::invalid_argument("invalid OrderStatus");
}

std::string canonical_order_string(const Order& order) {
  std::ostringstream os;
  os << order.order_id() << ',' << (order.side() == Side::Buy ? "buy" : "sell") << ','
     << (order.order_type() == OrderType::Market ? "market" : "limit") << ',' << order.original_quantity() << ','
     << order.filled_quantity() << ',' << order.remaining_quantity() << ',' << order.decision_timestamp_ns() << ','
     << order.submit_timestamp_ns() << ','
     << order.exchange_arrival_timestamp_ns() << ',';
  if (order.limit_price_ticks().has_value()) {
    os << *order.limit_price_ticks();
  }
  os << ',' << order_status_name(order.status());
  return os.str();
}

}  // namespace replay
