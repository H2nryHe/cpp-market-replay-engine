#include "replay/latency_execution.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace replay {
namespace {

std::string order_arrival_label(OrderId order_id) {
  std::ostringstream os;
  os << "order_id=" << order_id;
  return os.str();
}

std::string cancel_arrival_label(OrderId order_id) {
  std::ostringstream os;
  os << "cancel_order_id=" << order_id;
  return os.str();
}

Quantity checked_queue_ahead_from_128(__int128 value) {
  if (value > static_cast<__int128>(std::numeric_limits<Quantity>::max())) {
    throw std::overflow_error("queue_ahead exceeds Quantity range");
  }
  return static_cast<Quantity>(value);
}

bool limit_price_allows_level(const Order& order, PriceTicks level_price_ticks) {
  if (!order.limit_price_ticks().has_value()) {
    throw std::invalid_argument("limit order requires limit_price_ticks");
  }
  if (order.side() == Side::Buy) {
    return level_price_ticks <= *order.limit_price_ticks();
  }
  return level_price_ticks >= *order.limit_price_ticks();
}

std::vector<PriceLevel> opposite_limit_depth(const Order& order, const OrderBook& book) {
  if (order.side() == Side::Buy) {
    return book.top_asks(book.ask_level_count());
  }
  return book.top_bids(book.bid_level_count());
}

bool is_active_resting_status(OrderStatus status) noexcept {
  return status == OrderStatus::Acknowledged || status == OrderStatus::PartiallyFilled;
}

bool exact_price_trade_qualifies(const ActiveLimitOrder& active, const TradeEvent& trade) {
  if (!trade.aggressor_side.has_value()) {
    return false;
  }
  if (active.side == Side::Buy) {
    return *trade.aggressor_side == Side::Sell && trade.price_ticks == active.limit_price_ticks;
  }
  return *trade.aggressor_side == Side::Buy && trade.price_ticks == active.limit_price_ticks;
}

bool trade_through_qualifies(const ActiveLimitOrder& active, const TradeEvent& trade) {
  if (!trade.aggressor_side.has_value()) {
    return false;
  }
  if (active.side == Side::Buy) {
    return *trade.aggressor_side == Side::Sell && trade.price_ticks < active.limit_price_ticks;
  }
  return *trade.aggressor_side == Side::Buy && trade.price_ticks > active.limit_price_ticks;
}

class LatencyIntentSink final : public IntentSink {
 public:
  LatencyIntentSink(LatencyAwareExecution& execution, EventLoop& loop) : execution_{execution}, loop_{loop} {}

  void emit(OrderIntent intent) override {
    static_cast<void>(execution_.submit_order_intent(intent, loop_));
  }

 private:
  LatencyAwareExecution& execution_;
  EventLoop& loop_;
};

}  // namespace

QueueFraction::QueueFraction(std::int64_t ppm) : ppm_{ppm} {
  if (ppm_ < 0 || ppm_ > denominator_ppm) {
    throw std::invalid_argument("queue_fraction_ppm must be in [0, 1000000]");
  }
}

std::int64_t QueueFraction::ppm() const noexcept {
  return ppm_;
}

Quantity QueueFraction::initial_queue_ahead(Quantity visible_quantity) const {
  validate_quantity(visible_quantity);
  const auto numerator =
      (static_cast<__int128>(visible_quantity) * static_cast<__int128>(ppm_)) + (denominator_ppm / 2);
  return checked_queue_ahead_from_128(numerator / denominator_ppm);
}

LatencyAwareExecution::LatencyAwareExecution(LatencyExecutionConfig config)
    : config_{config}, queue_fraction_{config_.queue_fraction_ppm}, execution_simulator_{config_.execution_config} {}

const OrderSubmissionRecord& LatencyAwareExecution::submit_order_intent(const OrderIntent& intent, EventLoop& loop) {
  return submit_order_intent(intent, config_.order_latency, loop);
}

const OrderSubmissionRecord& LatencyAwareExecution::submit_order_intent(const OrderIntent& intent,
                                                                        LatencyNs latency,
                                                                        EventLoop& loop) {
  auto order = execution_simulator_.create_order_from_intent(intent, latency);
  order.transition_to(OrderStatus::Pending);
  const auto arrival_event = loop.schedule_internal(
      order.exchange_arrival_timestamp_ns(),
      InternalEvent{.type = InternalEventType::OrderArrival, .label = order_arrival_label(order.order_id()), .order_id = order.order_id()});

  orders_.push_back(order);
  submissions_.push_back(OrderSubmissionRecord{order, arrival_event});
  return submissions_.back();
}

ScheduledInternalEvent LatencyAwareExecution::request_cancel(OrderId order_id, EventLoop& loop) {
  return request_cancel(order_id, config_.cancel_latency, loop);
}

ScheduledInternalEvent LatencyAwareExecution::request_cancel(OrderId order_id, LatencyNs cancel_latency, EventLoop& loop) {
  const auto order_index = find_order_index(order_id);
  if (!is_active_resting_status(orders_[order_index].status())) {
    throw std::invalid_argument("cancel requires an active resting limit order");
  }
  static_cast<void>(find_active_limit_order_index(order_id));
  const auto cancel_timestamp_ns = checked_add_latency(loop.clock().now(), cancel_latency);
  return loop.schedule_internal(
      cancel_timestamp_ns,
      InternalEvent{.type = InternalEventType::CancelArrival, .label = cancel_arrival_label(order_id), .order_id = order_id});
}

const ExecutionResult& LatencyAwareExecution::process_order_arrival(const ScheduledInternalEvent& event,
                                                                    const OrderBook& book) {
  if (event.event.type != InternalEventType::OrderArrival || !event.event.order_id.has_value()) {
    throw std::invalid_argument("internal event is not an order arrival");
  }

  const auto order_index = find_order_index(*event.event.order_id);
  if (orders_[order_index].status() != OrderStatus::Pending) {
    throw std::invalid_argument("order arrival already processed or order is not pending");
  }
  if (event.timestamp_ns != orders_[order_index].exchange_arrival_timestamp_ns()) {
    throw std::invalid_argument("order arrival timestamp does not match order exchange arrival timestamp");
  }

  auto result = orders_[order_index].order_type() == OrderType::Limit
                    ? process_limit_order_arrival(orders_[order_index], book)
                    : execution_simulator_.execute_order(orders_[order_index], book);
  orders_[order_index] = result.order;
  for (const auto& fill : result.fills) {
    fills_.push_back(fill);
  }
  executions_.push_back(std::move(result));
  return executions_.back();
}

void LatencyAwareExecution::process_cancel_arrival(const ScheduledInternalEvent& event) {
  if (event.event.type != InternalEventType::CancelArrival || !event.event.order_id.has_value()) {
    throw std::invalid_argument("internal event is not a cancel arrival");
  }
  const auto order_index = find_order_index(*event.event.order_id);
  if (!is_active_resting_status(orders_[order_index].status())) {
    throw std::invalid_argument("cancel arrival requires an active resting limit order");
  }
  static_cast<void>(find_active_limit_order_index(*event.event.order_id));
  orders_[order_index].transition_to(OrderStatus::Canceled);
  deactivate_active_limit_order(*event.event.order_id);
}

void LatencyAwareExecution::process_trade(const TradeEvent& trade) {
  validate_quantity(trade.quantity);
  validate_price_ticks(trade.price_ticks);
  if (trade.quantity == 0 || !trade.aggressor_side.has_value()) {
    return;
  }

  Quantity remaining_trade_quantity = trade.quantity;
  for (auto& active : active_limit_orders_) {
    if (remaining_trade_quantity == 0) {
      break;
    }

    const bool exact_match = exact_price_trade_qualifies(active, trade);
    const bool trade_through = trade_through_qualifies(active, trade);
    if (!exact_match && !trade_through) {
      continue;
    }

    const auto order_index = find_order_index(active.order_id);
    auto& order = orders_[order_index];
    if (!is_active_resting_status(order.status())) {
      continue;
    }

    if (trade_through) {
      active.queue_ahead = 0;
    }

    const auto queue_consumed = std::min(active.queue_ahead, remaining_trade_quantity);
    active.queue_ahead -= queue_consumed;
    remaining_trade_quantity -= queue_consumed;
    if (remaining_trade_quantity == 0 || active.queue_ahead > 0) {
      continue;
    }

    const auto fill_quantity = std::min(order.remaining_quantity(), remaining_trade_quantity);
    if (fill_quantity == 0) {
      continue;
    }

    const auto fill = execution_simulator_.create_fill(order, active.limit_price_ticks, fill_quantity, trade.key.timestamp_ns);
    order.record_fill_quantity(fill_quantity);
    fills_.push_back(fill);
    remaining_trade_quantity -= fill_quantity;

    if (order.remaining_quantity() == 0) {
      order.transition_to(OrderStatus::Filled);
      active.queue_ahead = 0;
    } else if (order.status() == OrderStatus::Acknowledged) {
      order.transition_to(OrderStatus::PartiallyFilled);
    }
  }

  active_limit_orders_.erase(
      std::remove_if(active_limit_orders_.begin(),
                     active_limit_orders_.end(),
                     [this](const ActiveLimitOrder& active) {
                       const auto order_index = find_order_index(active.order_id);
                       return orders_[order_index].status() == OrderStatus::Filled ||
                              orders_[order_index].status() == OrderStatus::Canceled ||
                              orders_[order_index].status() == OrderStatus::Rejected;
                     }),
      active_limit_orders_.end());
}

const std::vector<OrderSubmissionRecord>& LatencyAwareExecution::submissions() const noexcept {
  return submissions_;
}

std::vector<Order> LatencyAwareExecution::final_orders() const {
  return orders_;
}

std::vector<ActiveLimitOrder> LatencyAwareExecution::active_limit_orders() const {
  return active_limit_orders_;
}

std::optional<ActiveLimitOrder> LatencyAwareExecution::active_limit_order(OrderId order_id) const {
  for (const auto& active : active_limit_orders_) {
    if (active.order_id == order_id) {
      return active;
    }
  }
  return std::nullopt;
}

const std::vector<ExecutionResult>& LatencyAwareExecution::executions() const noexcept {
  return executions_;
}

const std::vector<Fill>& LatencyAwareExecution::fills() const noexcept {
  return fills_;
}

const LatencyExecutionConfig& LatencyAwareExecution::config() const noexcept {
  return config_;
}

std::size_t LatencyAwareExecution::find_order_index(OrderId order_id) const {
  for (std::size_t index = 0; index < orders_.size(); ++index) {
    if (orders_[index].order_id() == order_id) {
      return index;
    }
  }
  throw std::invalid_argument("unknown order_id for arrival event");
}

std::size_t LatencyAwareExecution::find_active_limit_order_index(OrderId order_id) const {
  for (std::size_t index = 0; index < active_limit_orders_.size(); ++index) {
    if (active_limit_orders_[index].order_id == order_id) {
      return index;
    }
  }
  throw std::invalid_argument("order_id is not an active resting limit order");
}

Quantity LatencyAwareExecution::visible_quantity_at_limit(const Order& order, const OrderBook& book) const {
  if (!order.limit_price_ticks().has_value()) {
    throw std::invalid_argument("limit order requires limit_price_ticks");
  }
  const auto visible = order.side() == Side::Buy ? book.bid_quantity_at(*order.limit_price_ticks())
                                                 : book.ask_quantity_at(*order.limit_price_ticks());
  return visible.value_or(0);
}

ExecutionResult LatencyAwareExecution::process_limit_order_arrival(Order order, const OrderBook& book) {
  if (order.order_type() != OrderType::Limit) {
    throw std::invalid_argument("process_limit_order_arrival requires a limit order");
  }
  if (order.status() != OrderStatus::Pending) {
    throw std::invalid_argument("limit order must be pending at arrival");
  }

  order.transition_to(OrderStatus::Acknowledged);
  std::vector<Fill> aggressive_fills;
  bool filled_at_limit = false;

  for (const auto& level : opposite_limit_depth(order, book)) {
    validate_quantity(level.quantity);
    if (level.quantity == 0 || order.remaining_quantity() == 0) {
      continue;
    }
    if (!limit_price_allows_level(order, level.price_ticks)) {
      break;
    }

    const auto fill_quantity = std::min(order.remaining_quantity(), level.quantity);
    auto fill = execution_simulator_.create_fill(order, level.price_ticks, fill_quantity, order.exchange_arrival_timestamp_ns());
    order.record_fill_quantity(fill_quantity);
    if (level.price_ticks == *order.limit_price_ticks()) {
      filled_at_limit = true;
    }
    aggressive_fills.push_back(fill);
  }

  if (order.remaining_quantity() == 0) {
    order.transition_to(OrderStatus::Filled);
  } else {
    Quantity queue_ahead = 0;
    if (!filled_at_limit) {
      queue_ahead = queue_fraction_.initial_queue_ahead(visible_quantity_at_limit(order, book));
    }
    if (!aggressive_fills.empty()) {
      order.transition_to(OrderStatus::PartiallyFilled);
    }
    rest_limit_order(order, queue_ahead);
  }

  return ExecutionResult{order, aggressive_fills};
}

void LatencyAwareExecution::rest_limit_order(const Order& order, Quantity queue_ahead) {
  if (!is_active_resting_status(order.status())) {
    throw std::invalid_argument("only acknowledged or partially filled limit orders can rest");
  }
  if (!order.limit_price_ticks().has_value()) {
    throw std::invalid_argument("resting limit order requires limit_price_ticks");
  }
  validate_quantity(queue_ahead);
  active_limit_orders_.push_back(ActiveLimitOrder{.order_id = order.order_id(),
                                                 .side = order.side(),
                                                 .limit_price_ticks = *order.limit_price_ticks(),
                                                 .queue_ahead = queue_ahead,
                                                 .arrival_timestamp_ns = order.exchange_arrival_timestamp_ns()});
}

void LatencyAwareExecution::deactivate_active_limit_order(OrderId order_id) {
  const auto index = find_active_limit_order_index(order_id);
  active_limit_orders_.erase(active_limit_orders_.begin() + static_cast<std::ptrdiff_t>(index));
}

LatencyExecutionRunResult run_latency_aware_strategy(std::vector<MarketEvent> historical_events,
                                                     Strategy& strategy,
                                                     LatencyExecutionConfig config) {
  OrderBook book;
  EventLoop loop{std::move(historical_events)};
  LatencyAwareExecution execution{config};

  auto loop_result = loop.run(EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&strategy, &book, &execution](const MarketEvent& event, EventLoop& loop_ref) {
            LatencyIntentSink intents{execution, loop_ref};
            if (event.type() == MarketEventType::BookUpdate) {
              strategy.on_book(event.book_update(), book, intents);
              return;
            }
            execution.process_trade(event.trade());
            strategy.on_trade(event.trade(), book, intents);
          },
      .on_internal =
          [&strategy, &book, &execution](const ScheduledInternalEvent& event, EventLoop& loop_ref) {
            if (event.event.type == InternalEventType::OrderArrival) {
              static_cast<void>(execution.process_order_arrival(event, book));
              return;
            }
            if (event.event.type == InternalEventType::CancelArrival) {
              execution.process_cancel_arrival(event);
              return;
            }
            if (event.event.type == InternalEventType::Timer) {
              LatencyIntentSink intents{execution, loop_ref};
              strategy.on_timer(event.timestamp_ns, event.event, book, intents);
            }
          },
  });

  return LatencyExecutionRunResult{std::move(loop_result),
                                   std::move(book),
                                   execution.submissions(),
                                   execution.final_orders(),
                                   execution.executions(),
                                   execution.fills()};
}

}  // namespace replay
