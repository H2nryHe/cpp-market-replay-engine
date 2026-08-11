#include "replay/strategy.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace replay {
namespace {

std::string side_name(Side side) {
  switch (side) {
    case Side::Buy:
      return "buy";
    case Side::Sell:
      return "sell";
  }
  throw std::invalid_argument("invalid Side enum value");
}

std::string order_type_name(OrderType order_type) {
  switch (order_type) {
    case OrderType::Market:
      return "market";
    case OrderType::Limit:
      return "limit";
  }
  throw std::invalid_argument("invalid OrderType enum value");
}

}  // namespace

void VectorIntentSink::emit(OrderIntent intent) {
  intents_.push_back(intent);
}

const std::vector<OrderIntent>& VectorIntentSink::intents() const noexcept {
  return intents_;
}

bool VectorIntentSink::empty() const noexcept {
  return intents_.empty();
}

std::size_t VectorIntentSink::size() const noexcept {
  return intents_.size();
}

StrategyRunResult run_strategy(std::vector<MarketEvent> historical_events,
                               Strategy& strategy,
                               IntentSink& intents,
                               std::vector<StrategyTimer> timers) {
  OrderBook book;
  EventLoop loop{std::move(historical_events)};
  for (const auto& timer : timers) {
    loop.schedule_internal(timer.timestamp_ns, InternalEvent{InternalEventType::Timer, timer.label});
  }

  auto loop_result = loop.run(EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&strategy, &book, &intents](const MarketEvent& event, EventLoop&) {
            if (event.type() == MarketEventType::BookUpdate) {
              strategy.on_book(event.book_update(), book, intents);
              return;
            }
            strategy.on_trade(event.trade(), book, intents);
          },
      .on_internal =
          [&strategy, &book, &intents](const ScheduledInternalEvent& event, EventLoop&) {
            if (event.event.type == InternalEventType::Timer) {
              strategy.on_timer(event.timestamp_ns, event.event, book, intents);
            }
          },
  });

  return StrategyRunResult{std::move(loop_result), std::move(book)};
}

std::string canonical_intent_string(const OrderIntent& intent) {
  std::ostringstream os;
  os << side_name(intent.side) << ',' << intent.quantity << ',' << order_type_name(intent.order_type) << ',';
  if (intent.limit_price_ticks.has_value()) {
    os << *intent.limit_price_ticks;
  }
  os << ',' << intent.decision_timestamp_ns;
  return os.str();
}

}  // namespace replay
