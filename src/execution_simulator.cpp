#include "replay/execution_simulator.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace replay {
namespace {

FeeAmount checked_fee_from_128(__int128 value) {
  if (value > static_cast<__int128>(std::numeric_limits<FeeAmount>::max())) {
    throw std::overflow_error("fee_amount exceeds FeeAmount range");
  }
  return static_cast<FeeAmount>(value);
}

__int128 checked_notional(PriceTicks price_ticks, Quantity quantity) {
  validate_price_ticks(price_ticks);
  validate_quantity(quantity);
  return static_cast<__int128>(price_ticks) * static_cast<__int128>(quantity);
}

__int128 checked_fee_numerator(__int128 notional, std::int64_t fee_rate_ppm) {
  if (fee_rate_ppm == 0) {
    return 0;
  }

  constexpr auto half_denominator = static_cast<__int128>(FeeModel::denominator_ppm / 2);
  const auto max_before_rounding = std::numeric_limits<__int128>::max() - half_denominator;
  if (notional > max_before_rounding / static_cast<__int128>(fee_rate_ppm)) {
    throw std::overflow_error("fee calculation overflow");
  }
  return (notional * static_cast<__int128>(fee_rate_ppm)) + half_denominator;
}

std::vector<PriceLevel> opposite_depth(const Order& order, const OrderBook& book) {
  if (order.side() == Side::Buy) {
    return book.top_asks(book.ask_level_count());
  }
  return book.top_bids(book.bid_level_count());
}

void ensure_pending_for_execution(Order& order) {
  if (order.status() == OrderStatus::New) {
    order.transition_to(OrderStatus::Pending);
    return;
  }
  if (order.status() != OrderStatus::Pending) {
    throw std::invalid_argument("order must be New or Pending before execution");
  }
}

}  // namespace

FeeModel::FeeModel(std::int64_t fee_rate_ppm) : fee_rate_ppm_{fee_rate_ppm} {
  if (fee_rate_ppm_ < 0) {
    throw std::invalid_argument("fee_rate_ppm must be non-negative");
  }
  if (fee_rate_ppm_ > denominator_ppm) {
    throw std::invalid_argument("fee_rate_ppm must not exceed 1000000");
  }
}

std::int64_t FeeModel::fee_rate_ppm() const noexcept {
  return fee_rate_ppm_;
}

FeeAmount FeeModel::calculate_fee(PriceTicks price_ticks, Quantity quantity) const {
  const auto notional = checked_notional(price_ticks, quantity);
  const auto numerator = checked_fee_numerator(notional, fee_rate_ppm_);
  return checked_fee_from_128(numerator / denominator_ppm);
}

Order OrderFactory::create_order(Side side,
                                 OrderType order_type,
                                 Quantity quantity,
                                 TimestampNs submit_timestamp_ns,
                                 std::optional<PriceTicks> limit_price_ticks) {
  return create_order_with_latency(side, order_type, quantity, submit_timestamp_ns, LatencyNs{}, limit_price_ticks);
}

Order OrderFactory::create_order_with_latency(Side side,
                                              OrderType order_type,
                                              Quantity quantity,
                                              TimestampNs submit_timestamp_ns,
                                              LatencyNs latency,
                                              std::optional<PriceTicks> limit_price_ticks) {
  const auto exchange_arrival_timestamp_ns = checked_add_latency(submit_timestamp_ns, latency);
  const auto order_id = next_order_id_;
  ++next_order_id_;
  return Order{order_id, side, order_type, quantity, submit_timestamp_ns, exchange_arrival_timestamp_ns, limit_price_ticks};
}

Order OrderFactory::create_order_from_intent(const OrderIntent& intent) {
  return create_order(intent.side, intent.order_type, intent.quantity, intent.decision_timestamp_ns, intent.limit_price_ticks);
}

Order OrderFactory::create_order_from_intent(const OrderIntent& intent, LatencyNs latency) {
  return create_order_with_latency(
      intent.side, intent.order_type, intent.quantity, intent.decision_timestamp_ns, latency, intent.limit_price_ticks);
}

ExecutionSimulator::ExecutionSimulator(ExecutionConfig config) : config_{config} {}

Order ExecutionSimulator::create_order_from_intent(const OrderIntent& intent) {
  return order_factory_.create_order_from_intent(intent);
}

Order ExecutionSimulator::create_order_from_intent(const OrderIntent& intent, LatencyNs latency) {
  return order_factory_.create_order_from_intent(intent, latency);
}

Order ExecutionSimulator::create_order(Side side,
                                       OrderType order_type,
                                       Quantity quantity,
                                       TimestampNs submit_timestamp_ns,
                                       std::optional<PriceTicks> limit_price_ticks) {
  return order_factory_.create_order(side, order_type, quantity, submit_timestamp_ns, limit_price_ticks);
}

Order ExecutionSimulator::create_order_with_latency(Side side,
                                                    OrderType order_type,
                                                    Quantity quantity,
                                                    TimestampNs submit_timestamp_ns,
                                                    LatencyNs latency,
                                                    std::optional<PriceTicks> limit_price_ticks) {
  return order_factory_.create_order_with_latency(side, order_type, quantity, submit_timestamp_ns, latency, limit_price_ticks);
}

ExecutionResult ExecutionSimulator::execute_order(Order order, const OrderBook& book) {
  ensure_pending_for_execution(order);
  if (order.order_type() == OrderType::Limit) {
    order.transition_to(OrderStatus::Rejected);
    return ExecutionResult{order, {}};
  }
  return execute_market_order(std::move(order), book);
}

ExecutionResult ExecutionSimulator::execute_market_order(Order order, const OrderBook& book) {
  if (order.order_type() != OrderType::Market) {
    throw std::invalid_argument("execute_market_order requires a market order");
  }

  ensure_pending_for_execution(order);
  const auto depth = opposite_depth(order, book);
  if (depth.empty()) {
    order.transition_to(OrderStatus::Rejected);
    return ExecutionResult{order, {}};
  }

  order.transition_to(OrderStatus::Acknowledged);
  std::vector<Fill> fills;

  for (const auto& level : depth) {
    validate_quantity(level.quantity);
    if (level.quantity == 0 || order.remaining_quantity() == 0) {
      continue;
    }

    const auto fill_quantity = std::min(order.remaining_quantity(), level.quantity);
    auto fill = make_fill(order, level.price_ticks, fill_quantity);
    order.record_fill_quantity(fill_quantity);
    fills.push_back(fill);

    if (order.remaining_quantity() == 0) {
      break;
    }
  }

  if (fills.empty()) {
    order.transition_to(OrderStatus::Rejected);
    return ExecutionResult{order, fills};
  }

  if (order.remaining_quantity() == 0) {
    order.transition_to(OrderStatus::Filled);
  } else {
    order.transition_to(OrderStatus::PartiallyFilled);
    order.transition_to(OrderStatus::Canceled);
  }

  return ExecutionResult{order, fills};
}

const ExecutionConfig& ExecutionSimulator::config() const noexcept {
  return config_;
}

Fill ExecutionSimulator::create_fill(const Order& order,
                                     PriceTicks price_ticks,
                                     Quantity quantity,
                                     TimestampNs fill_timestamp_ns) {
  validate_price_ticks(price_ticks);
  validate_quantity(quantity);
  if (quantity <= 0) {
    throw std::invalid_argument("fill quantity must be greater than zero");
  }
  const Fill fill{
      .order_id = order.order_id(),
      .side = order.side(),
      .price_ticks = price_ticks,
      .quantity = quantity,
      .fill_timestamp_ns = fill_timestamp_ns,
      .fill_sequence_id = next_fill_sequence_id_,
      .fee_amount = config_.fee_model.calculate_fee(price_ticks, quantity),
  };
  ++next_fill_sequence_id_;
  return fill;
}

Fill ExecutionSimulator::make_fill(const Order& order, PriceTicks price_ticks, Quantity quantity) {
  return create_fill(order, price_ticks, quantity, order.exchange_arrival_timestamp_ns());
}

TimestampNs checked_add_latency(TimestampNs submit_timestamp_ns, LatencyNs latency) {
  if (latency.count > std::numeric_limits<TimestampNs>::max() - submit_timestamp_ns) {
    throw std::overflow_error("exchange_arrival_timestamp_ns overflow");
  }
  return submit_timestamp_ns + latency.count;
}

}  // namespace replay
