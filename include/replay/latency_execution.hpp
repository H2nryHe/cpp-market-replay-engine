#pragma once

#include "replay/event_loop.hpp"
#include "replay/execution_simulator.hpp"
#include "replay/fill.hpp"
#include "replay/order.hpp"
#include "replay/order_book.hpp"
#include "replay/strategy.hpp"
#include "replay/types.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace replay {

struct LatencyExecutionConfig {
  LatencyNs order_latency{};
  ExecutionConfig execution_config{};
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

class LatencyAwareExecution {
 public:
  explicit LatencyAwareExecution(LatencyExecutionConfig config = {});

  const OrderSubmissionRecord& submit_order_intent(const OrderIntent& intent, EventLoop& loop);
  const OrderSubmissionRecord& submit_order_intent(const OrderIntent& intent, LatencyNs latency, EventLoop& loop);
  const ExecutionResult& process_order_arrival(const ScheduledInternalEvent& event, const OrderBook& book);

  [[nodiscard]] const std::vector<OrderSubmissionRecord>& submissions() const noexcept;
  [[nodiscard]] std::vector<Order> final_orders() const;
  [[nodiscard]] const std::vector<ExecutionResult>& executions() const noexcept;
  [[nodiscard]] const std::vector<Fill>& fills() const noexcept;
  [[nodiscard]] const LatencyExecutionConfig& config() const noexcept;

 private:
  [[nodiscard]] std::size_t find_order_index(OrderId order_id) const;

  LatencyExecutionConfig config_{};
  ExecutionSimulator execution_simulator_;
  std::vector<Order> orders_;
  std::vector<OrderSubmissionRecord> submissions_;
  std::vector<ExecutionResult> executions_;
  std::vector<Fill> fills_;
};

LatencyExecutionRunResult run_latency_aware_strategy(std::vector<MarketEvent> historical_events,
                                                     Strategy& strategy,
                                                     LatencyExecutionConfig config = {});

}  // namespace replay
