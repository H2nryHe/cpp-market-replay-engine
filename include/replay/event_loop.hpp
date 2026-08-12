#pragma once

#include "replay/market_feed.hpp"
#include "replay/order_book.hpp"
#include "replay/simulation_clock.hpp"
#include "replay/types.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace replay {

enum class InternalEventType {
  Timer,
  OrderArrival,
  CancelArrival,
  User,
};

enum class TraceEventClass {
  Market,
  Internal,
};

enum class TraceEventKind {
  BookUpdate,
  Trade,
  Timer,
  OrderArrival,
  CancelArrival,
  User,
};

struct InternalEvent {
  InternalEventType type{InternalEventType::User};
  std::string label{};
  std::optional<OrderId> order_id{};

  friend bool operator==(const InternalEvent&, const InternalEvent&) = default;
};

struct ScheduledInternalEvent {
  TimestampNs timestamp_ns{};
  std::uint64_t internal_sequence_id{};
  InternalEvent event{};

  friend bool operator==(const ScheduledInternalEvent&, const ScheduledInternalEvent&) = default;
};

struct EventTraceEntry {
  TraceEventClass event_class{TraceEventClass::Market};
  TraceEventKind kind{TraceEventKind::BookUpdate};
  TimestampNs timestamp_ns{};
  std::uint64_t sequence_id{};
  std::string label{};

  friend bool operator==(const EventTraceEntry&, const EventTraceEntry&) = default;
};

class EventLoop;

struct EventLoopHandlers {
  OrderBook* order_book{nullptr};
  std::function<void(const MarketEvent&, EventLoop&)> on_market{};
  std::function<void(const ScheduledInternalEvent&, EventLoop&)> on_internal{};
};

struct EventLoopResult {
  std::vector<EventTraceEntry> trace{};
  TimestampNs final_time_ns{};
  std::size_t processed_event_count{};

  [[nodiscard]] std::string canonical_trace() const;
  [[nodiscard]] std::uint64_t trace_hash() const;
};

class InternalEventScheduler {
 public:
  ScheduledInternalEvent schedule(TimestampNs timestamp_ns, InternalEvent event, TimestampNs current_time_ns);

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const ScheduledInternalEvent& next() const;
  ScheduledInternalEvent pop_next();

 private:
  struct LaterScheduledEvent {
    bool operator()(const ScheduledInternalEvent& lhs, const ScheduledInternalEvent& rhs) const noexcept;
  };

  std::uint64_t next_internal_sequence_id_{0};
  std::priority_queue<ScheduledInternalEvent, std::vector<ScheduledInternalEvent>, LaterScheduledEvent> queue_;
};

class EventLoop {
 public:
  explicit EventLoop(std::vector<MarketEvent> historical_events);

  [[nodiscard]] const SimulationClock& clock() const noexcept;
  [[nodiscard]] SimulationClock& clock() noexcept;

  ScheduledInternalEvent schedule_internal(TimestampNs timestamp_ns, InternalEvent event);
  [[nodiscard]] bool has_pending_internal_events() const noexcept;

  EventLoopResult run(EventLoopHandlers handlers = {});

 private:
  [[nodiscard]] bool should_process_market_next() const;
  [[nodiscard]] const MarketEvent& next_market_event() const;

  std::vector<MarketEvent> historical_events_;
  std::size_t next_market_index_{0};
  SimulationClock clock_{};
  InternalEventScheduler scheduler_{};
};

std::string canonical_trace_line(const EventTraceEntry& entry);

}  // namespace replay
