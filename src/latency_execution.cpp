#include "replay/latency_execution.hpp"

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

LatencyAwareExecution::LatencyAwareExecution(LatencyExecutionConfig config)
    : config_{config}, execution_simulator_{config_.execution_config} {}

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

  auto result = execution_simulator_.execute_order(orders_[order_index], book);
  orders_[order_index] = result.order;
  for (const auto& fill : result.fills) {
    fills_.push_back(fill);
  }
  executions_.push_back(std::move(result));
  return executions_.back();
}

const std::vector<OrderSubmissionRecord>& LatencyAwareExecution::submissions() const noexcept {
  return submissions_;
}

std::vector<Order> LatencyAwareExecution::final_orders() const {
  return orders_;
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
            strategy.on_trade(event.trade(), book, intents);
          },
      .on_internal =
          [&strategy, &book, &execution](const ScheduledInternalEvent& event, EventLoop& loop_ref) {
            if (event.event.type == InternalEventType::OrderArrival) {
              static_cast<void>(execution.process_order_arrival(event, book));
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
