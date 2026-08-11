#pragma once

#include "replay/strategy.hpp"

#include <cstddef>

namespace replay {

struct QueueImbalanceConfig {
  std::size_t depth_levels{1};
  double buy_threshold{0.25};
  double sell_threshold{-0.25};
  Quantity order_quantity{1};
  OrderType order_type{OrderType::Market};
  std::optional<PriceTicks> limit_price_ticks{};
};

class QueueImbalanceStrategy final : public Strategy {
 public:
  explicit QueueImbalanceStrategy(QueueImbalanceConfig config);

  void on_book(const BookUpdateEvent& event, const OrderBook& book, IntentSink& intents) override;
  void on_trade(const TradeEvent& event, const OrderBook& book, IntentSink& intents) override;
  void on_timer(TimestampNs timestamp_ns, const InternalEvent& event, const OrderBook& book, IntentSink& intents) override;

  [[nodiscard]] const QueueImbalanceConfig& config() const noexcept;
  [[nodiscard]] std::optional<double> last_queue_imbalance() const noexcept;

 private:
  [[nodiscard]] std::optional<double> compute_queue_imbalance(const OrderBook& book) const;
  void validate_config() const;

  QueueImbalanceConfig config_;
  std::optional<double> last_queue_imbalance_{};
};

}  // namespace replay
