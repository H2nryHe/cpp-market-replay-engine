#include "replay/event_loop.hpp"

#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace replay {
namespace {

constexpr std::uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t fnv1a_prime = 1099511628211ULL;

TraceEventKind trace_kind_for_market(MarketEventType type) {
  switch (type) {
    case MarketEventType::BookUpdate:
      return TraceEventKind::BookUpdate;
    case MarketEventType::Trade:
      return TraceEventKind::Trade;
  }
  throw std::invalid_argument("invalid MarketEventType");
}

TraceEventKind trace_kind_for_internal(InternalEventType type) {
  switch (type) {
    case InternalEventType::Timer:
      return TraceEventKind::Timer;
    case InternalEventType::OrderArrival:
      return TraceEventKind::OrderArrival;
    case InternalEventType::CancelArrival:
      return TraceEventKind::CancelArrival;
    case InternalEventType::User:
      return TraceEventKind::User;
  }
  throw std::invalid_argument("invalid InternalEventType");
}

std::string trace_kind_name(TraceEventKind kind) {
  switch (kind) {
    case TraceEventKind::BookUpdate:
      return "book_update";
    case TraceEventKind::Trade:
      return "trade";
    case TraceEventKind::Timer:
      return "timer";
    case TraceEventKind::OrderArrival:
      return "order_arrival";
    case TraceEventKind::CancelArrival:
      return "cancel_arrival";
    case TraceEventKind::User:
      return "user";
  }
  throw std::invalid_argument("invalid TraceEventKind");
}

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = fnv1a_offset_basis;
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= fnv1a_prime;
  }
  return hash;
}

EventTraceEntry trace_market_event(EventKey key, MarketEventType type) {
  return EventTraceEntry{
      .event_class = TraceEventClass::Market,
      .kind = trace_kind_for_market(type),
      .timestamp_ns = key.timestamp_ns,
      .sequence_id = key.sequence_id,
      .label = {},
  };
}

EventTraceEntry trace_internal_event(const ScheduledInternalEvent& event) {
  return EventTraceEntry{
      .event_class = TraceEventClass::Internal,
      .kind = trace_kind_for_internal(event.event.type),
      .timestamp_ns = event.timestamp_ns,
      .sequence_id = event.internal_sequence_id,
      .label = event.event.label,
  };
}

}  // namespace

std::string EventLoopResult::canonical_trace() const {
  std::ostringstream os;
  for (const auto& entry : trace) {
    os << canonical_trace_line(entry) << '\n';
  }
  return os.str();
}

std::uint64_t EventLoopResult::trace_hash() const {
  return fnv1a64(canonical_trace());
}

bool InternalEventScheduler::LaterScheduledEvent::operator()(const ScheduledInternalEvent& lhs,
                                                             const ScheduledInternalEvent& rhs) const noexcept {
  if (lhs.timestamp_ns != rhs.timestamp_ns) {
    return lhs.timestamp_ns > rhs.timestamp_ns;
  }
  return lhs.internal_sequence_id > rhs.internal_sequence_id;
}

ScheduledInternalEvent InternalEventScheduler::schedule(TimestampNs timestamp_ns,
                                                        InternalEvent event,
                                                        TimestampNs current_time_ns) {
  if (timestamp_ns < current_time_ns) {
    throw std::invalid_argument("cannot schedule internal event in the past");
  }

  const ScheduledInternalEvent scheduled{
      .timestamp_ns = timestamp_ns,
      .internal_sequence_id = next_internal_sequence_id_,
      .event = std::move(event),
  };
  ++next_internal_sequence_id_;
  queue_.push(scheduled);
  return scheduled;
}

bool InternalEventScheduler::empty() const noexcept {
  return queue_.empty();
}

const ScheduledInternalEvent& InternalEventScheduler::next() const {
  if (queue_.empty()) {
    throw std::logic_error("internal scheduler is empty");
  }
  return queue_.top();
}

ScheduledInternalEvent InternalEventScheduler::pop_next() {
  if (queue_.empty()) {
    throw std::logic_error("internal scheduler is empty");
  }
  auto next_event = queue_.top();
  queue_.pop();
  return next_event;
}

EventLoop::EventLoop(std::vector<MarketEvent> historical_events) : historical_events_{std::move(historical_events)} {}

const SimulationClock& EventLoop::clock() const noexcept {
  return clock_;
}

SimulationClock& EventLoop::clock() noexcept {
  return clock_;
}

ScheduledInternalEvent EventLoop::schedule_internal(TimestampNs timestamp_ns, InternalEvent event) {
  return scheduler_.schedule(timestamp_ns, std::move(event), clock_.now());
}

bool EventLoop::has_pending_internal_events() const noexcept {
  return !scheduler_.empty();
}

EventLoopResult EventLoop::run(EventLoopHandlers handlers) {
  EventLoopResult result;
  result.trace.reserve(historical_events_.size());

  while (next_market_index_ < historical_events_.size() || !scheduler_.empty()) {
    if (should_process_market_next()) {
      const auto& event = next_market_event();
      const auto key = event.key();
      const auto type = event.type();
      clock_.advance_to(key.timestamp_ns);
      if (handlers.order_book != nullptr && type == MarketEventType::BookUpdate) {
        handlers.order_book->apply(event.book_update());
      }
      result.trace.push_back(trace_market_event(key, type));
      ++next_market_index_;
      if (handlers.on_market) {
        handlers.on_market(event, *this);
      }
      continue;
    }

    auto event = scheduler_.pop_next();
    clock_.advance_to(event.timestamp_ns);
    result.trace.push_back(trace_internal_event(event));
    if (handlers.on_internal) {
      handlers.on_internal(event, *this);
    }
  }

  result.final_time_ns = clock_.now();
  result.processed_event_count = result.trace.size();
  return result;
}

bool EventLoop::should_process_market_next() const {
  if (next_market_index_ >= historical_events_.size()) {
    return false;
  }
  if (scheduler_.empty()) {
    return true;
  }

  const auto& market = next_market_event();
  const auto& internal = scheduler_.next();
  return market.key().timestamp_ns <= internal.timestamp_ns;
}

const MarketEvent& EventLoop::next_market_event() const {
  if (next_market_index_ >= historical_events_.size()) {
    throw std::logic_error("historical market stream is exhausted");
  }
  return historical_events_[next_market_index_];
}

std::string canonical_trace_line(const EventTraceEntry& entry) {
  std::ostringstream os;
  if (entry.event_class == TraceEventClass::Market) {
    if (entry.kind != TraceEventKind::BookUpdate && entry.kind != TraceEventKind::Trade) {
      throw std::invalid_argument("market trace entry has non-market kind");
    }
    os << "M," << entry.timestamp_ns << ',' << trace_kind_name(entry.kind) << ',' << entry.sequence_id;
    return os.str();
  }

  if (entry.kind == TraceEventKind::BookUpdate || entry.kind == TraceEventKind::Trade) {
    throw std::invalid_argument("internal trace entry has market kind");
  }
  os << "I," << entry.timestamp_ns << ',' << trace_kind_name(entry.kind) << ',' << entry.sequence_id << ','
     << entry.label;
  return os.str();
}

}  // namespace replay
