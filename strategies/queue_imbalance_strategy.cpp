#include "queue_imbalance_strategy.hpp"

#include <limits>
#include <stdexcept>

namespace replay {
namespace {

Quantity sum_quantity(const std::vector<PriceLevel>& levels) {
  Quantity total = 0;
  for (const auto& level : levels) {
    if (level.quantity > std::numeric_limits<Quantity>::max() - total) {
      throw std::overflow_error("queue imbalance volume overflow");
    }
    total += level.quantity;
  }
  return total;
}

}  // namespace

QueueImbalanceStrategy::QueueImbalanceStrategy(QueueImbalanceConfig config) : config_{config} {
  validate_config();
}

void QueueImbalanceStrategy::on_book(const BookUpdateEvent& event, const OrderBook& book, IntentSink& intents) {
  last_queue_imbalance_ = compute_queue_imbalance(book);
  if (!last_queue_imbalance_.has_value()) {
    return;
  }

  if (*last_queue_imbalance_ > config_.buy_threshold) {
    intents.emit(OrderIntent{Side::Buy, config_.order_quantity, config_.order_type, config_.limit_price_ticks,
                             event.key.timestamp_ns});
    return;
  }

  if (*last_queue_imbalance_ < config_.sell_threshold) {
    intents.emit(OrderIntent{Side::Sell, config_.order_quantity, config_.order_type, config_.limit_price_ticks,
                             event.key.timestamp_ns});
  }
}

void QueueImbalanceStrategy::on_trade(const TradeEvent&, const OrderBook&, IntentSink&) {}

void QueueImbalanceStrategy::on_timer(TimestampNs, const InternalEvent&, const OrderBook&, IntentSink&) {}

const QueueImbalanceConfig& QueueImbalanceStrategy::config() const noexcept {
  return config_;
}

std::optional<double> QueueImbalanceStrategy::last_queue_imbalance() const noexcept {
  return last_queue_imbalance_;
}

std::optional<double> QueueImbalanceStrategy::compute_queue_imbalance(const OrderBook& book) const {
  if (config_.depth_levels == 0 || book.best_bid() == std::nullopt || book.best_ask() == std::nullopt) {
    return std::nullopt;
  }
  if (book.is_locked() || book.is_crossed()) {
    return std::nullopt;
  }

  const auto bid_volume = sum_quantity(book.top_bids(config_.depth_levels));
  const auto ask_volume = sum_quantity(book.top_asks(config_.depth_levels));
  if (ask_volume > std::numeric_limits<Quantity>::max() - bid_volume) {
    throw std::overflow_error("queue imbalance total volume overflow");
  }
  const auto total_volume = bid_volume + ask_volume;
  if (total_volume == 0) {
    return std::nullopt;
  }

  return static_cast<double>(bid_volume - ask_volume) / static_cast<double>(total_volume);
}

void QueueImbalanceStrategy::validate_config() const {
  if (config_.order_quantity <= 0) {
    throw std::invalid_argument("queue imbalance order_quantity must be positive");
  }
  if (config_.sell_threshold > config_.buy_threshold) {
    throw std::invalid_argument("queue imbalance sell_threshold must be <= buy_threshold");
  }
}

}  // namespace replay
