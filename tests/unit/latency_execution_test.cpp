#include "replay/latency_execution.hpp"
#include "replay/market_feed.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect_true(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

template <typename Exception, typename Callable>
void expect_throws(Callable callable, std::string_view message) {
  try {
    callable();
  } catch (const Exception&) {
    return;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << message << " threw wrong exception: " << error.what() << '\n';
    ++failures;
    return;
  }

  std::cerr << "FAIL: " << message << " did not throw\n";
  ++failures;
}

replay::MarketEvent book_event(replay::TimestampNs timestamp_ns,
                               std::uint64_t sequence_id,
                               replay::Side side,
                               replay::PriceTicks price_ticks,
                               replay::Quantity quantity) {
  return replay::MarketEvent{
      replay::BookUpdateEvent{replay::EventKey{timestamp_ns, sequence_id}, side, price_ticks, quantity}};
}

replay::OrderIntent buy_market_intent(replay::TimestampNs timestamp_ns, replay::Quantity quantity = 1) {
  return replay::OrderIntent{.side = replay::Side::Buy,
                             .quantity = quantity,
                             .order_type = replay::OrderType::Market,
                             .limit_price_ticks = std::nullopt,
                             .decision_timestamp_ns = timestamp_ns};
}

std::vector<std::string> fill_strings(const std::vector<replay::Fill>& fills) {
  std::vector<std::string> output;
  output.reserve(fills.size());
  for (const auto& fill : fills) {
    output.push_back(replay::canonical_fill_string(fill));
  }
  return output;
}

std::vector<std::string> status_names(const replay::Order& order) {
  std::vector<std::string> output;
  output.reserve(order.status_history().size());
  for (const auto status : order.status_history()) {
    output.push_back(replay::order_status_name(status));
  }
  return output;
}

class SequenceIntentStrategy final : public replay::Strategy {
 public:
  explicit SequenceIntentStrategy(std::vector<std::uint64_t> book_sequence_ids, replay::Quantity quantity = 1)
      : book_sequence_ids_{std::move(book_sequence_ids)}, quantity_{quantity} {}

  void on_book(const replay::BookUpdateEvent& event,
               const replay::OrderBook&,
               replay::IntentSink& intents) override {
    if (std::find(book_sequence_ids_.begin(), book_sequence_ids_.end(), event.key.sequence_id) !=
        book_sequence_ids_.end()) {
      intents.emit(buy_market_intent(event.key.timestamp_ns, quantity_));
    }
  }

  void on_trade(const replay::TradeEvent&, const replay::OrderBook&, replay::IntentSink&) override {}
  void on_timer(replay::TimestampNs, const replay::InternalEvent&, const replay::OrderBook&, replay::IntentSink&) override {}

 private:
  std::vector<std::uint64_t> book_sequence_ids_;
  replay::Quantity quantity_{1};
};

void test_arrival_time_calculation() {
  expect_true(replay::checked_add_latency(1'000, replay::LatencyNs::from_nanoseconds(500)) == 1'500,
              "arrival timestamp is submit timestamp plus latency");
}

void test_zero_latency_not_inline_and_pending_until_arrival() {
  replay::OrderBook book;
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Sell, 10001, 5)}};
  replay::LatencyAwareExecution execution{replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs{}}};
  bool observed_pending_before_arrival = false;

  static_cast<void>(loop.run(replay::EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&execution, &observed_pending_before_arrival](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
            const auto& submission = execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), loop_ref);
            observed_pending_before_arrival = submission.order.status() == replay::OrderStatus::Pending &&
                                              submission.order.filled_quantity() == 0 && execution.executions().empty() &&
                                              execution.fills().empty() &&
                                              submission.arrival_event.timestamp_ns == event.key().timestamp_ns;
          },
      .on_internal =
          [&execution, &book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
            static_cast<void>(execution.process_order_arrival(event, book));
          },
  }));

  expect_true(observed_pending_before_arrival, "zero latency schedules arrival and leaves order pending before internal event");
  expect_true(execution.executions().size() == 1, "zero-latency order executes from internal event");
  expect_true(execution.final_orders().front().status() == replay::OrderStatus::Filled, "zero-latency final status");
}

void test_zero_latency_same_timestamp_market_precedence() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {
          book_event(100, 1, replay::Side::Sell, 10001, 5),
          book_event(100, 2, replay::Side::Sell, 10001, 0),
          book_event(100, 3, replay::Side::Sell, 10005, 5),
      },
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs{}});

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10005,1,100,0"}),
              "zero-latency arrival sees same-timestamp market updates before internal event");
  expect_true(result.event_loop_result.canonical_trace() ==
                  "M,100,book_update,1\nM,100,book_update,2\nM,100,book_update,3\nI,100,order_arrival,0,order_id=1\n",
              "zero-latency trace preserves market-before-internal precedence");
}

void test_book_moves_during_latency() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {
          book_event(100, 1, replay::Side::Sell, 10001, 5),
          book_event(150, 2, replay::Side::Sell, 10001, 0),
          book_event(170, 3, replay::Side::Sell, 10005, 5),
      },
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(100)});

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10005,1,200,0"}),
              "delayed order fills against arrival-time book state");
}

void test_intervening_events_process_before_execution() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {
          book_event(100, 1, replay::Side::Sell, 10001, 5),
          book_event(200, 2, replay::Side::Sell, 10001, 0),
          book_event(300, 3, replay::Side::Sell, 10003, 5),
          book_event(400, 4, replay::Side::Sell, 10004, 5),
      },
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(400)});

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10003,1,500,0"}),
              "all intervening market events process before arrival");
  expect_true(result.event_loop_result.canonical_trace().find("M,400,book_update,4\nI,500,order_arrival,0,order_id=1") !=
                  std::string::npos,
              "arrival trace occurs after intervening market events");
}

void test_lifecycle_full_and_insufficient() {
  SequenceIntentStrategy full_strategy{{1}};
  const auto full = replay::run_latency_aware_strategy(
      {book_event(100, 1, replay::Side::Sell, 10001, 5)},
      full_strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(50)});
  expect_true((status_names(full.final_orders.front()) ==
               std::vector<std::string>{"new", "pending", "acknowledged", "filled"}),
              "latency full fill lifecycle");

  SequenceIntentStrategy partial_strategy{{1}, 8};
  const auto partial = replay::run_latency_aware_strategy(
      {book_event(100, 1, replay::Side::Sell, 10001, 5)},
      partial_strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(50)});
  expect_true((status_names(partial.final_orders.front()) ==
               std::vector<std::string>{"new", "pending", "acknowledged", "partially_filled", "canceled"}),
              "latency insufficient liquidity lifecycle");
}

void test_multiple_orders_different_arrival_times() {
  replay::OrderBook book;
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Sell, 10001, 5),
                          book_event(200, 2, replay::Side::Buy, 9999, 5)}};
  replay::LatencyAwareExecution execution;

  static_cast<void>(loop.run(replay::EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
            if (event.key().sequence_id == 1) {
              static_cast<void>(
                  execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(300), loop_ref));
            }
            if (event.key().sequence_id == 2) {
              static_cast<void>(
                  execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(100), loop_ref));
            }
          },
      .on_internal =
          [&execution, &book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
            static_cast<void>(execution.process_order_arrival(event, book));
          },
  }));

  expect_true(execution.executions().size() == 2, "two different-arrival orders execute");
  expect_true(execution.executions()[0].order.order_id() == 2 && execution.executions()[1].order.order_id() == 1,
              "different arrival times process by arrival timestamp, not order ID");
}

void test_multiple_orders_same_arrival_time() {
  std::optional<std::vector<replay::OrderId>> expected_order_ids;
  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::OrderBook book;
    replay::EventLoop loop{{book_event(100, 1, replay::Side::Sell, 10001, 5)}};
    replay::LatencyAwareExecution execution;

    static_cast<void>(loop.run(replay::EventLoopHandlers{
        .order_book = &book,
        .on_market =
            [&execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
              static_cast<void>(
                  execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(400), loop_ref));
              static_cast<void>(
                  execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(400), loop_ref));
            },
        .on_internal =
            [&execution, &book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
              static_cast<void>(execution.process_order_arrival(event, book));
            },
    }));

    std::vector<replay::OrderId> order_ids;
    for (const auto& execution_result : execution.executions()) {
      order_ids.push_back(execution_result.order.order_id());
    }
    if (!expected_order_ids.has_value()) {
      expected_order_ids = order_ids;
    } else {
      expect_true(order_ids == expected_order_ids, "same-arrival internal insertion ordering is deterministic");
    }
    expect_true((order_ids == std::vector<replay::OrderId>{1, 2}), "same-arrival processing follows submission order");
  }
}

void test_latency_sweep_and_synthetic_fixture() {
  const replay::FeedParserConfig parser_config{.tick_size = "0.01", .price_format = replay::PriceFieldFormat::Ticks};
  const auto feed = replay::load_book_updates_csv("tests/fixtures/latency_execution_book_updates.csv", parser_config);
  const std::vector<std::int64_t> latency_us_values{0, 50, 100, 500, 1'000, 5'000};

  for (const auto latency_us : latency_us_values) {
    SequenceIntentStrategy strategy{{1}};
    const auto latency = replay::LatencyNs::from_microseconds(latency_us);
    const auto result =
        replay::run_latency_aware_strategy(feed.events(), strategy, replay::LatencyExecutionConfig{.order_latency = latency});

    const auto expected_arrival = replay::checked_add_latency(100, latency);
    expect_true(result.submissions.front().order.exchange_arrival_timestamp_ns() == expected_arrival,
                "latency sweep exact arrival timestamp");
    expect_true(result.final_orders.front().exchange_arrival_timestamp_ns() == expected_arrival,
                "latency sweep final order arrival timestamp");
    expect_true(result.event_loop_result.final_time_ns >= expected_arrival, "latency sweep event loop reaches arrival");
    expect_true(result.final_orders.front().status() == replay::OrderStatus::Filled, "latency sweep deterministic execution");
  }

  SequenceIntentStrategy zero_strategy{{1}};
  const auto zero_result = replay::run_latency_aware_strategy(
      feed.events(), zero_strategy, replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_microseconds(0)});
  SequenceIntentStrategy delayed_strategy{{1}};
  const auto delayed_result = replay::run_latency_aware_strategy(
      feed.events(), delayed_strategy, replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(100)});
  expect_true(zero_result.fills.front().price_ticks == 10001 && delayed_result.fills.front().price_ticks == 10005,
              "synthetic latency fixture changes execution price");
}

void test_negative_latency_and_overflow() {
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::LatencyNs::from_nanoseconds(-1)); },
                                       "negative latency rejected before construction");
  expect_throws<std::overflow_error>(
      [] {
        static_cast<void>(
            replay::checked_add_latency(std::numeric_limits<replay::TimestampNs>::max(), replay::LatencyNs{1}));
      },
      "timestamp plus latency overflow rejected");
}

void test_end_of_feed_pending_order_executes() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {book_event(100, 1, replay::Side::Sell, 10001, 5)},
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(100)});

  expect_true(result.event_loop_result.final_time_ns == 200, "pending order executes after historical feed exhaustion");
  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10001,1,200,0"}),
              "end-of-feed arrival executes against final book");
}

void test_empty_book_at_arrival() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {book_event(100, 1, replay::Side::Sell, 10001, 5), book_event(150, 2, replay::Side::Sell, 10001, 0)},
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(100)});

  expect_true(result.fills.empty(), "no fills when opposite liquidity disappears before arrival");
  expect_true(result.final_orders.front().status() == replay::OrderStatus::Rejected, "empty arrival book uses no-liquidity policy");
}

void test_delayed_execution_keeps_historical_book_immutable() {
  SequenceIntentStrategy strategy{{1}};
  const auto result = replay::run_latency_aware_strategy(
      {book_event(100, 1, replay::Side::Sell, 10001, 5), book_event(150, 2, replay::Side::Sell, 10005, 7)},
      strategy,
      replay::LatencyExecutionConfig{.order_latency = replay::LatencyNs::from_nanoseconds(100)});

  expect_true(!result.fills.empty(), "immutability delayed execution generated fill");
  expect_true(result.final_book.ask_quantity_at(10001) == 5 && result.final_book.ask_quantity_at(10005) == 7,
              "delayed execution does not mutate historical book levels");
}

void test_order_arrival_executes_once() {
  replay::OrderBook book;
  book.apply(replay::BookUpdateEvent{replay::EventKey{0, 1}, replay::Side::Sell, 10001, 5});
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  replay::LatencyAwareExecution execution;
  const auto& submission = execution.submit_order_intent(buy_market_intent(0), replay::LatencyNs{}, loop);
  const auto arrival = submission.arrival_event;

  static_cast<void>(execution.process_order_arrival(arrival, book));
  expect_throws<std::invalid_argument>([&execution, &arrival, &book] { static_cast<void>(execution.process_order_arrival(arrival, book)); },
                                       "duplicate order arrival rejected");
}

void test_determinism() {
  std::optional<std::string> expected_trace;
  std::optional<std::vector<std::string>> expected_fills;
  std::optional<std::vector<std::string>> expected_orders;
  std::optional<std::uint64_t> expected_book_hash;

  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::OrderBook book;
    replay::EventLoop loop{{book_event(100, 1, replay::Side::Sell, 10001, 5),
                            book_event(100, 2, replay::Side::Sell, 10002, 5),
                            book_event(150, 3, replay::Side::Sell, 10001, 0),
                            book_event(200, 4, replay::Side::Sell, 10005, 5)}};
    replay::LatencyAwareExecution execution{replay::LatencyExecutionConfig{
        .execution_config = replay::ExecutionConfig{.fee_model = replay::FeeModel{100}}}};

    const auto loop_result = loop.run(replay::EventLoopHandlers{
        .order_book = &book,
        .on_market =
            [&execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
              if (event.key().sequence_id == 1) {
                static_cast<void>(
                    execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(200), loop_ref));
                static_cast<void>(
                    execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(200), loop_ref));
              }
              if (event.key().sequence_id == 4) {
                static_cast<void>(
                    execution.submit_order_intent(buy_market_intent(event.key().timestamp_ns), replay::LatencyNs::from_nanoseconds(50), loop_ref));
              }
            },
        .on_internal =
            [&execution, &book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
              static_cast<void>(execution.process_order_arrival(event, book));
            },
    });

    std::vector<std::string> orders;
    for (const auto& order : execution.final_orders()) {
      orders.push_back(replay::canonical_order_string(order));
    }
    const auto trace = loop_result.canonical_trace();
    const auto fills = fill_strings(execution.fills());
    const auto book_hash = book.state_hash();

    if (!expected_trace.has_value()) {
      expected_trace = trace;
      expected_fills = fills;
      expected_orders = orders;
      expected_book_hash = book_hash;
    } else {
      expect_true(trace == expected_trace, "deterministic arrival-event trace");
      expect_true(fills == expected_fills, "deterministic fill sequence and fees");
      expect_true(orders == expected_orders, "deterministic order IDs and lifecycle outcomes");
      expect_true(book_hash == expected_book_hash, "deterministic final book hash");
    }
  }
}

}  // namespace

int main() {
  test_arrival_time_calculation();
  test_zero_latency_not_inline_and_pending_until_arrival();
  test_zero_latency_same_timestamp_market_precedence();
  test_book_moves_during_latency();
  test_intervening_events_process_before_execution();
  test_lifecycle_full_and_insufficient();
  test_multiple_orders_different_arrival_times();
  test_multiple_orders_same_arrival_time();
  test_latency_sweep_and_synthetic_fixture();
  test_negative_latency_and_overflow();
  test_end_of_feed_pending_order_executes();
  test_empty_book_at_arrival();
  test_delayed_execution_keeps_historical_book_immutable();
  test_order_arrival_executes_once();
  test_determinism();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
