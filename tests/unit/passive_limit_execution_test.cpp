#include "replay/latency_execution.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

replay::BookUpdateEvent update(replay::Side side, replay::PriceTicks price_ticks, replay::Quantity quantity) {
  return replay::BookUpdateEvent{replay::EventKey{}, side, price_ticks, quantity};
}

replay::OrderBook book_from(std::initializer_list<replay::BookUpdateEvent> updates) {
  replay::OrderBook book;
  for (const auto& event : updates) {
    book.apply(event);
  }
  return book;
}

replay::OrderIntent limit_intent(replay::Side side,
                                 replay::PriceTicks limit_price_ticks,
                                 replay::Quantity quantity,
                                 replay::TimestampNs timestamp_ns = 100) {
  return replay::OrderIntent{.side = side,
                             .quantity = quantity,
                             .order_type = replay::OrderType::Limit,
                             .limit_price_ticks = limit_price_ticks,
                             .decision_timestamp_ns = timestamp_ns};
}

replay::TradeEvent trade(replay::TimestampNs timestamp_ns,
                         std::uint64_t sequence_id,
                         replay::PriceTicks price_ticks,
                         replay::Quantity quantity,
                         std::optional<replay::Side> aggressor_side) {
  return replay::TradeEvent{replay::EventKey{timestamp_ns, sequence_id}, price_ticks, quantity, aggressor_side};
}

std::vector<std::string> fill_strings(const std::vector<replay::Fill>& fills) {
  std::vector<std::string> output;
  for (const auto& fill : fills) {
    output.push_back(replay::canonical_fill_string(fill));
  }
  return output;
}

replay::LatencyAwareExecution arrived_limit_execution(replay::OrderBook& book,
                                                      const replay::OrderIntent& intent,
                                                      replay::LatencyExecutionConfig config = {}) {
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  replay::LatencyAwareExecution execution{config};
  const auto& submission = execution.submit_order_intent(intent, replay::LatencyNs{}, loop);
  static_cast<void>(execution.process_order_arrival(submission.arrival_event, book));
  return execution;
}

void test_non_marketable_limits_rest() {
  auto buy_book = book_from({update(replay::Side::Sell, 10005, 5), update(replay::Side::Buy, 10000, 100)});
  auto buy_execution = arrived_limit_execution(
      buy_book, limit_intent(replay::Side::Buy, 10000, 7), replay::LatencyExecutionConfig{.queue_fraction_ppm = 500'000});
  expect_true(buy_execution.fills().empty(), "non-marketable buy has no aggressive fills");
  expect_true(buy_execution.final_orders().front().remaining_quantity() == 7, "non-marketable buy remains unfilled");
  expect_true(buy_execution.final_orders().front().status() == replay::OrderStatus::Acknowledged,
              "non-marketable buy is active acknowledged");
  expect_true(buy_execution.active_limit_orders().size() == 1, "non-marketable buy rests");

  auto sell_book = book_from({update(replay::Side::Buy, 9995, 5), update(replay::Side::Sell, 10000, 100)});
  auto sell_execution = arrived_limit_execution(sell_book, limit_intent(replay::Side::Sell, 10000, 7));
  expect_true(sell_execution.fills().empty(), "non-marketable sell has no aggressive fills");
  expect_true(sell_execution.active_limit_orders().size() == 1, "non-marketable sell rests");
}

void test_marketable_limits_and_remainder() {
  auto buy_book =
      book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3), update(replay::Side::Sell, 10003, 5)});
  auto buy_execution = arrived_limit_execution(buy_book, limit_intent(replay::Side::Buy, 10002, 4));
  expect_true((fill_strings(buy_execution.fills()) ==
               std::vector<std::string>{"1,1,buy,10001,2,100,0", "2,1,buy,10002,2,100,0"}),
              "marketable buy limit respects price protection");
  expect_true(buy_execution.final_orders().front().status() == replay::OrderStatus::Filled, "marketable buy filled");

  auto remainder_book =
      book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3), update(replay::Side::Sell, 10003, 5)});
  auto remainder_execution = arrived_limit_execution(remainder_book, limit_intent(replay::Side::Buy, 10002, 8));
  expect_true(remainder_execution.final_orders().front().filled_quantity() == 5, "marketable buy remainder aggressive fill qty");
  expect_true(remainder_execution.final_orders().front().remaining_quantity() == 3, "marketable buy remainder rests qty");
  expect_true(remainder_execution.final_orders().front().status() == replay::OrderStatus::PartiallyFilled,
              "marketable buy remainder remains active");
  expect_true(remainder_execution.active_limit_orders().size() == 1, "marketable buy remainder active");

  auto sell_book =
      book_from({update(replay::Side::Buy, 10000, 2), update(replay::Side::Buy, 9999, 3), update(replay::Side::Buy, 9998, 5)});
  auto sell_execution = arrived_limit_execution(sell_book, limit_intent(replay::Side::Sell, 9999, 4));
  expect_true((fill_strings(sell_execution.fills()) ==
               std::vector<std::string>{"1,1,sell,10000,2,100,0", "2,1,sell,9999,2,100,0"}),
              "marketable sell limit respects price protection");
}

void test_historical_book_immutability() {
  auto book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 5)});
  const auto before_hash = book.state_hash();
  auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 10),
                                           replay::LatencyExecutionConfig{.queue_fraction_ppm = 500'000});
  execution.process_trade(trade(200, 1, 10000, 55, replay::Side::Sell));
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  const auto cancel_event = execution.request_cancel(1, replay::LatencyNs{}, loop);
  execution.process_cancel_arrival(cancel_event);

  expect_true(book.state_hash() == before_hash, "simulated rest/fill/cancel does not mutate historical book");
  expect_true(book.bid_quantity_at(10000) == 100, "historical bid quantity unchanged");
}

void test_queue_fraction_values_and_validation() {
  expect_true(replay::QueueFraction{0}.initial_queue_ahead(100) == 0, "queue fraction 0");
  expect_true(replay::QueueFraction{250'000}.initial_queue_ahead(100) == 25, "queue fraction 0.25");
  expect_true(replay::QueueFraction{500'000}.initial_queue_ahead(100) == 50, "queue fraction 0.50");
  expect_true(replay::QueueFraction{750'000}.initial_queue_ahead(100) == 75, "queue fraction 0.75");
  expect_true(replay::QueueFraction{1'000'000}.initial_queue_ahead(100) == 100, "queue fraction 1.00");
  expect_throws<std::invalid_argument>([] { replay::QueueFraction invalid{-1}; }, "negative queue fraction rejected");
  expect_throws<std::invalid_argument>([] { replay::QueueFraction invalid{1'000'001}; },
                                       "queue fraction above one rejected");
}

void test_exact_price_queue_consumption() {
  auto book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 1)});
  auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 20),
                                           replay::LatencyExecutionConfig{.queue_fraction_ppm = 500'000});

  execution.process_trade(trade(200, 1, 10000, 30, replay::Side::Sell));
  expect_true(execution.active_limit_order(1)->queue_ahead == 20, "first trade reduces queue ahead only");
  expect_true(execution.fills().empty(), "first queue-consuming trade has no fill");

  execution.process_trade(trade(300, 2, 10000, 25, replay::Side::Sell));
  expect_true(execution.active_limit_order(1)->queue_ahead == 0, "second trade exhausts queue ahead");
  expect_true((fill_strings(execution.fills()) == std::vector<std::string>{"1,1,buy,10000,5,300,0"}),
              "second trade residual fills simulated order");
  expect_true(execution.final_orders().front().remaining_quantity() == 15, "passive partial remaining");
}

void test_wrong_and_unknown_aggressor_do_not_fill() {
  auto buy_book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 1)});
  auto buy_execution = arrived_limit_execution(buy_book, limit_intent(replay::Side::Buy, 10000, 20),
                                               replay::LatencyExecutionConfig{.queue_fraction_ppm = 500'000});
  buy_execution.process_trade(trade(200, 1, 10000, 100, replay::Side::Buy));
  buy_execution.process_trade(trade(210, 2, 10000, 100, std::nullopt));
  expect_true(buy_execution.active_limit_order(1)->queue_ahead == 50, "wrong/unknown buy aggressor leaves queue unchanged");
  expect_true(buy_execution.fills().empty(), "wrong/unknown buy aggressor has no fill");

  auto sell_book = book_from({update(replay::Side::Sell, 10000, 100), update(replay::Side::Buy, 9995, 1)});
  auto sell_execution = arrived_limit_execution(sell_book, limit_intent(replay::Side::Sell, 10000, 20),
                                                replay::LatencyExecutionConfig{.queue_fraction_ppm = 500'000});
  sell_execution.process_trade(trade(200, 1, 10000, 100, replay::Side::Sell));
  expect_true(sell_execution.active_limit_order(1)->queue_ahead == 50, "wrong sell aggressor leaves queue unchanged");
  expect_true(sell_execution.fills().empty(), "wrong sell aggressor has no fill");
}

void test_book_updates_do_not_consume_queue_or_fill() {
  auto book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 1)});
  auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 20),
                                           replay::LatencyExecutionConfig{.queue_fraction_ppm = 1'000'000});
  book.apply(update(replay::Side::Buy, 10000, 20));
  expect_true(execution.active_limit_order(1)->queue_ahead == 100, "book size reduction does not consume queue");
  expect_true(execution.fills().empty(), "book size reduction does not fill");
  book.apply(update(replay::Side::Buy, 10000, 0));
  expect_true(execution.active_limit_order(1)->queue_ahead == 100, "book level deletion does not consume queue");
  expect_true(execution.fills().empty(), "book level deletion does not create fill");
}

void test_partial_passive_fills_and_trade_through() {
  auto book = book_from({update(replay::Side::Buy, 10000, 0), update(replay::Side::Sell, 10005, 1)});
  auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 10));
  execution.process_trade(trade(200, 1, 10000, 3, replay::Side::Sell));
  execution.process_trade(trade(300, 2, 10000, 2, replay::Side::Sell));
  execution.process_trade(trade(400, 3, 10000, 20, replay::Side::Sell));
  expect_true((fill_strings(execution.fills()) == std::vector<std::string>{"1,1,buy,10000,3,200,0",
                                                                           "2,1,buy,10000,2,300,0",
                                                                           "3,1,buy,10000,5,400,0"}),
              "repeated partial passive fills");
  expect_true(execution.final_orders().front().status() == replay::OrderStatus::Filled, "partial passive fills final status");

  auto through_buy_book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 1)});
  auto through_buy = arrived_limit_execution(through_buy_book, limit_intent(replay::Side::Buy, 10000, 4),
                                             replay::LatencyExecutionConfig{.queue_fraction_ppm = 1'000'000});
  through_buy.process_trade(trade(200, 1, 9999, 4, replay::Side::Sell));
  expect_true((fill_strings(through_buy.fills()) == std::vector<std::string>{"1,1,buy,10000,4,200,0"}),
              "buy trade-through fills at limit price");

  auto through_sell_book = book_from({update(replay::Side::Sell, 10000, 100), update(replay::Side::Buy, 9995, 1)});
  auto through_sell = arrived_limit_execution(through_sell_book, limit_intent(replay::Side::Sell, 10000, 4),
                                              replay::LatencyExecutionConfig{.queue_fraction_ppm = 1'000'000});
  through_sell.process_trade(trade(200, 1, 10001, 4, replay::Side::Buy));
  expect_true((fill_strings(through_sell.fills()) == std::vector<std::string>{"1,1,sell,10000,4,200,0"}),
              "sell trade-through fills at limit price");
}

void test_cancellation_policies() {
  auto book = book_from({update(replay::Side::Buy, 10000, 0), update(replay::Side::Sell, 10005, 1)});
  auto active = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 10));
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  const auto cancel_event = active.request_cancel(1, replay::LatencyNs{}, loop);
  active.process_cancel_arrival(cancel_event);
  active.process_trade(trade(200, 1, 10000, 10, replay::Side::Sell));
  expect_true(active.final_orders().front().status() == replay::OrderStatus::Canceled, "active order cancels");
  expect_true(active.fills().empty(), "canceled order has no future fills");

  auto partial = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 10));
  partial.process_trade(trade(200, 1, 10000, 3, replay::Side::Sell));
  replay::EventLoop partial_loop{std::vector<replay::MarketEvent>{}};
  const auto partial_cancel = partial.request_cancel(1, replay::LatencyNs{}, partial_loop);
  partial.process_cancel_arrival(partial_cancel);
  expect_true(partial.final_orders().front().filled_quantity() == 3, "partial cancel preserves prior fills");
  expect_true(partial.final_orders().front().status() == replay::OrderStatus::Canceled, "partial order canceled");

  expect_throws<std::invalid_argument>([&partial, &partial_cancel] { partial.process_cancel_arrival(partial_cancel); },
                                       "cancel terminal canceled order rejected");

  auto filled = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 2));
  filled.process_trade(trade(200, 1, 10000, 2, replay::Side::Sell));
  replay::EventLoop filled_loop{std::vector<replay::MarketEvent>{}};
  expect_throws<std::invalid_argument>([&filled, &filled_loop] { static_cast<void>(filled.request_cancel(1, filled_loop)); },
                                       "cancel filled order rejected");

  replay::OrderBook empty_book;
  replay::EventLoop rejected_loop{std::vector<replay::MarketEvent>{}};
  replay::LatencyAwareExecution rejected;
  const auto rejected_arrival =
      rejected.submit_order_intent(replay::OrderIntent{.side = replay::Side::Buy,
                                                       .quantity = 1,
                                                       .order_type = replay::OrderType::Market,
                                                       .limit_price_ticks = std::nullopt,
                                                       .decision_timestamp_ns = 100},
                                   replay::LatencyNs{},
                                   rejected_loop)
          .arrival_event;
  static_cast<void>(rejected.process_order_arrival(rejected_arrival, empty_book));
  expect_throws<std::invalid_argument>([&rejected, &rejected_loop] { static_cast<void>(rejected.request_cancel(1, rejected_loop)); },
                                       "cancel rejected order rejected");
}

void test_same_timestamp_races() {
  replay::OrderBook book;
  replay::EventLoop loop{{replay::MarketEvent{replay::BookUpdateEvent{replay::EventKey{100, 1}, replay::Side::Buy, 10000, 0}},
                          replay::MarketEvent{trade(200, 2, 10000, 1, replay::Side::Sell)}}};
  replay::LatencyAwareExecution execution;

  static_cast<void>(loop.run(replay::EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
            if (event.type() == replay::MarketEventType::BookUpdate) {
              const auto& submission =
                  execution.submit_order_intent(limit_intent(replay::Side::Buy, 10000, 5, event.key().timestamp_ns), replay::LatencyNs{}, loop_ref);
              static_cast<void>(submission);
            } else {
              execution.process_trade(event.trade());
            }
          },
      .on_internal =
          [&execution, &book](const replay::ScheduledInternalEvent& event, replay::EventLoop& loop_ref) {
            if (event.event.type == replay::InternalEventType::OrderArrival) {
              static_cast<void>(execution.process_order_arrival(event, book));
              static_cast<void>(execution.request_cancel(1, replay::LatencyNs::from_nanoseconds(100), loop_ref));
            } else if (event.event.type == replay::InternalEventType::CancelArrival) {
              execution.process_cancel_arrival(event);
            }
          },
  }));

  expect_true((fill_strings(execution.fills()) == std::vector<std::string>{"1,1,buy,10000,1,200,0"}),
              "same-timestamp trade before cancel fills first");
  expect_true(execution.final_orders().front().status() == replay::OrderStatus::Canceled,
              "same-timestamp cancel applies after trade");

  replay::OrderBook order_arrival_book;
  replay::EventLoop order_arrival_loop{{replay::MarketEvent{trade(200, 1, 10000, 5, replay::Side::Sell)}}};
  replay::LatencyAwareExecution order_arrival_execution;
  static_cast<void>(order_arrival_loop.run(replay::EventLoopHandlers{
      .order_book = &order_arrival_book,
      .on_market =
          [&order_arrival_execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
            order_arrival_execution.process_trade(event.trade());
            static_cast<void>(order_arrival_execution.submit_order_intent(limit_intent(replay::Side::Buy, 10000, 5, 200), replay::LatencyNs{}, loop_ref));
          },
      .on_internal =
          [&order_arrival_execution, &order_arrival_book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
            static_cast<void>(order_arrival_execution.process_order_arrival(event, order_arrival_book));
          },
  }));
  expect_true(order_arrival_execution.fills().empty(), "same-timestamp trade before order arrival cannot fill new order");
  expect_true(order_arrival_execution.final_orders().front().status() == replay::OrderStatus::Acknowledged,
              "new order rests after prior same-timestamp trade");
}

void test_trade_volume_conservation_and_determinism() {
  for (int iteration = 0; iteration < 100; ++iteration) {
    auto book = book_from({update(replay::Side::Buy, 10000, 0), update(replay::Side::Sell, 10005, 1)});
    replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
    replay::LatencyAwareExecution execution;
    const auto first_arrival =
        execution.submit_order_intent(limit_intent(replay::Side::Buy, 10000, 7), replay::LatencyNs{}, loop).arrival_event;
    const auto second_arrival =
        execution.submit_order_intent(limit_intent(replay::Side::Buy, 10000, 7), replay::LatencyNs{}, loop).arrival_event;
    static_cast<void>(execution.process_order_arrival(first_arrival, book));
    static_cast<void>(execution.process_order_arrival(second_arrival, book));
    execution.process_trade(trade(200, 1, 10000, 10, replay::Side::Sell));

    expect_true(execution.fills().size() == 2, "two active orders receive conserved trade volume");
    expect_true(execution.fills()[0].quantity + execution.fills()[1].quantity == 10,
                "passive fills do not exceed reported trade quantity");
    expect_true(execution.fills()[0].order_id == 1 && execution.fills()[0].quantity == 7, "own orders process by order ID");
    expect_true(execution.fills()[1].order_id == 2 && execution.fills()[1].quantity == 3, "residual trade volume reaches second order");
  }
}

void test_queue_fraction_sensitivity_and_conservation() {
  std::optional<replay::Quantity> previous_filled;
  for (const auto ppm : std::vector<std::int64_t>{250'000, 500'000, 750'000, 1'000'000}) {
    auto book = book_from({update(replay::Side::Buy, 10000, 100), update(replay::Side::Sell, 10005, 1)});
    auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10000, 20),
                                             replay::LatencyExecutionConfig{.queue_fraction_ppm = ppm});
    execution.process_trade(trade(200, 1, 10000, 60, replay::Side::Sell));
    const auto filled = execution.final_orders().front().filled_quantity();
    if (previous_filled.has_value()) {
      expect_true(filled <= *previous_filled, "larger queue fraction does not produce earlier/larger fill");
    }
    previous_filled = filled;
  }

  auto book = book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3), update(replay::Side::Buy, 10000, 0)});
  auto execution = arrived_limit_execution(book, limit_intent(replay::Side::Buy, 10002, 8));
  execution.process_trade(trade(200, 1, 10002, 20, replay::Side::Sell));
  const auto order = execution.final_orders().front();
  replay::Quantity fill_sum = 0;
  for (const auto& fill : execution.fills()) {
    fill_sum += fill.quantity;
  }
  expect_true(order.filled_quantity() <= order.original_quantity(), "no overfill across aggressive and passive fills");
  expect_true(fill_sum == order.filled_quantity(), "fill conservation across aggressive and passive fills");
}

void test_market_and_latency_regressions() {
  auto market_book = book_from({update(replay::Side::Sell, 10001, 5)});
  replay::ExecutionSimulator simulator;
  const auto market_result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 3, 100), market_book);
  expect_true((fill_strings(market_result.fills) == std::vector<std::string>{"1,1,buy,10001,3,100,0"}),
              "market order regression unchanged");

  replay::OrderBook latency_book;
  replay::EventLoop loop{{replay::MarketEvent{replay::BookUpdateEvent{replay::EventKey{100, 1}, replay::Side::Sell, 10001, 5}},
                          replay::MarketEvent{replay::BookUpdateEvent{replay::EventKey{150, 2}, replay::Side::Sell, 10001, 0}},
                          replay::MarketEvent{replay::BookUpdateEvent{replay::EventKey{170, 3}, replay::Side::Sell, 10005, 5}}}};
  replay::LatencyAwareExecution execution;
  static_cast<void>(loop.run(replay::EventLoopHandlers{
      .order_book = &latency_book,
      .on_market =
          [&execution](const replay::MarketEvent& event, replay::EventLoop& loop_ref) {
            if (event.key().sequence_id == 1) {
              static_cast<void>(execution.submit_order_intent(limit_intent(replay::Side::Buy, 10005, 1, event.key().timestamp_ns),
                                                             replay::LatencyNs::from_nanoseconds(100),
                                                             loop_ref));
            }
          },
      .on_internal =
          [&execution, &latency_book](const replay::ScheduledInternalEvent& event, replay::EventLoop&) {
            static_cast<void>(execution.process_order_arrival(event, latency_book));
          },
  }));
  expect_true((fill_strings(execution.fills()) == std::vector<std::string>{"1,1,buy,10005,1,200,0"}),
              "limit order arrival uses arrival-time book");
}

}  // namespace

int main() {
  test_non_marketable_limits_rest();
  test_marketable_limits_and_remainder();
  test_historical_book_immutability();
  test_queue_fraction_values_and_validation();
  test_exact_price_queue_consumption();
  test_wrong_and_unknown_aggressor_do_not_fill();
  test_book_updates_do_not_consume_queue_or_fill();
  test_partial_passive_fills_and_trade_through();
  test_cancellation_policies();
  test_same_timestamp_races();
  test_trade_volume_conservation_and_determinism();
  test_queue_fraction_sensitivity_and_conservation();
  test_market_and_latency_regressions();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
