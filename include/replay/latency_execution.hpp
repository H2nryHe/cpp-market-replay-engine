#pragma once

#include "replay/event_loop.hpp"
#include "replay/execution_simulator.hpp"
#include "replay/fill.hpp"
#include "replay/order.hpp"
#include "replay/order_book.hpp"
#include "replay/strategy.hpp"
#include "replay/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace replay {

struct LatencyExecutionConfig {
  LatencyNs order_latency{};
  ExecutionConfig execution_config{};
  std::int64_t queue_fraction_ppm{0};
  LatencyNs cancel_latency{};
};

class QueueFraction {
 public:
  static constexpr std::int64_t denominator_ppm = 1'000'000;

  explicit QueueFraction(std::int64_t ppm = 0);

  [[nodiscard]] std::int64_t ppm() const noexcept;
  [[nodiscard]] Quantity initial_queue_ahead(Quantity visible_quantity) const;

 private:
  std::int64_t ppm_{};
};

struct OrderSubmissionRecord {
  Order order;
  ScheduledInternalEvent arrival_event;
};

struct LatencyExecutionRunResult {
  EventLoopResult event_loop_result{};
  OrderBook final_book{};
  std::vector<OrderSubmissionRecord> submissions{};
  std::vector<Order> final_orders{};
  std::vector<ExecutionResult> executions{};
  std::vector<Fill> fills{};
};

struct ActiveLimitOrder {
  OrderId order_id{};
  Side side{};
  PriceTicks limit_price_ticks{};
  Quantity queue_ahead{};
  TimestampNs arrival_timestamp_ns{};

  friend constexpr bool operator==(const ActiveLimitOrder&, const ActiveLimitOrder&) = default;
};

class LatencyAwareExecution {
 public:
  explicit LatencyAwareExecution(LatencyExecutionConfig config = {});

  const OrderSubmissionRecord& submit_order_intent(const OrderIntent& intent, EventLoop& loop);
  const OrderSubmissionRecord& submit_order_intent(const OrderIntent& intent, LatencyNs latency, EventLoop& loop);
  ScheduledInternalEvent request_cancel(OrderId order_id, EventLoop& loop);
  ScheduledInternalEvent request_cancel(OrderId order_id, LatencyNs cancel_latency, EventLoop& loop);
  const ExecutionResult& process_order_arrival(const ScheduledInternalEvent& event, const OrderBook& book);
  void process_cancel_arrival(const ScheduledInternalEvent& event);
  void process_trade(const TradeEvent& trade);

  [[nodiscard]] const std::vector<OrderSubmissionRecord>& submissions() const noexcept;
  [[nodiscard]] std::vector<Order> final_orders() const;
  [[nodiscard]] std::vector<ActiveLimitOrder> active_limit_orders() const;
  [[nodiscard]] std::optional<ActiveLimitOrder> active_limit_order(OrderId order_id) const;
  [[nodiscard]] const std::vector<ExecutionResult>& executions() const noexcept;
  [[nodiscard]] const std::vector<Fill>& fills() const noexcept;
  [[nodiscard]] const LatencyExecutionConfig& config() const noexcept;

 private:
  [[nodiscard]] std::size_t find_order_index(OrderId order_id) const;
  [[nodiscard]] std::size_t find_active_limit_order_index(OrderId order_id) const;
  [[nodiscard]] Quantity visible_quantity_at_limit(const Order& order, const OrderBook& book) const;
  ExecutionResult process_limit_order_arrival(Order order, const OrderBook& book);
  void rest_limit_order(const Order& order, Quantity queue_ahead);
  void deactivate_active_limit_order(OrderId order_id);

  LatencyExecutionConfig config_{};
  QueueFraction queue_fraction_{};
  ExecutionSimulator execution_simulator_;
  std::vector<Order> orders_;
  std::vector<OrderSubmissionRecord> submissions_;
  std::vector<ExecutionResult> executions_;
  std::vector<Fill> fills_;
  std::vector<ActiveLimitOrder> active_limit_orders_;
};

LatencyExecutionRunResult run_latency_aware_strategy(std::vector<MarketEvent> historical_events,
                                                     Strategy& strategy,
                                                     LatencyExecutionConfig config = {});

}  // namespace replay
