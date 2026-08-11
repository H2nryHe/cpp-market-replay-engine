#pragma once

#include "replay/event_loop.hpp"
#include "replay/market_feed.hpp"
#include "replay/order_book.hpp"
#include "replay/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace replay {

struct OrderIntent {
  Side side{};
  Quantity quantity{};
  OrderType order_type{OrderType::Market};
  std::optional<PriceTicks> limit_price_ticks{};
  TimestampNs decision_timestamp_ns{};

  friend bool operator==(const OrderIntent&, const OrderIntent&) = default;
};

class IntentSink {
 public:
  virtual ~IntentSink() = default;
  virtual void emit(OrderIntent intent) = 0;
};

class VectorIntentSink final : public IntentSink {
 public:
  void emit(OrderIntent intent) override;

  [[nodiscard]] const std::vector<OrderIntent>& intents() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::vector<OrderIntent> intents_;
};

class Strategy {
 public:
  virtual ~Strategy() = default;

  virtual void on_book(const BookUpdateEvent& event, const OrderBook& book, IntentSink& intents) = 0;
  virtual void on_trade(const TradeEvent& event, const OrderBook& book, IntentSink& intents) = 0;
  virtual void on_timer(TimestampNs timestamp_ns, const InternalEvent& event, const OrderBook& book, IntentSink& intents) = 0;
};

struct StrategyRunResult {
  EventLoopResult event_loop_result{};
  OrderBook final_book{};
};

struct StrategyTimer {
  TimestampNs timestamp_ns{};
  std::string label{};
};

StrategyRunResult run_strategy(std::vector<MarketEvent> historical_events,
                               Strategy& strategy,
                               IntentSink& intents,
                               std::vector<StrategyTimer> timers = {});

std::string canonical_intent_string(const OrderIntent& intent);

}  // namespace replay
