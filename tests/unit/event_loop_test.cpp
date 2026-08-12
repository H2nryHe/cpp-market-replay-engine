#include "replay/event_loop.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
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

replay::MarketEvent book_event(replay::TimestampNs timestamp_ns,
                               std::uint64_t sequence_id,
                               replay::Side side,
                               replay::PriceTicks price_ticks,
                               replay::Quantity quantity) {
  return replay::MarketEvent{
      replay::BookUpdateEvent{replay::EventKey{timestamp_ns, sequence_id}, side, price_ticks, quantity}};
}

replay::MarketEvent trade_event(replay::TimestampNs timestamp_ns,
                                std::uint64_t sequence_id,
                                replay::PriceTicks price_ticks,
                                replay::Quantity quantity) {
  return replay::MarketEvent{replay::TradeEvent{replay::EventKey{timestamp_ns, sequence_id}, price_ticks, quantity,
                                                replay::Side::Buy}};
}

std::vector<std::string> trace_lines(const replay::EventLoopResult& result) {
  std::vector<std::string> lines;
  for (const auto& entry : result.trace) {
    lines.push_back(replay::canonical_trace_line(entry));
  }
  return lines;
}

void test_clock_monotonicity() {
  replay::SimulationClock clock;
  clock.advance_to(100);
  clock.advance_to(150);
  clock.advance_to(200);
  expect_true(clock.now() == 200, "clock advances monotonically");
  expect_throws<std::invalid_argument>([&clock] { clock.advance_to(199); }, "clock rejects backwards advancement");
}

void test_basic_historical_replay() {
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Buy, 10000, 1),
                          book_event(120, 2, replay::Side::Sell, 10001, 1),
                          trade_event(140, 3, 10001, 2)}};
  const auto result = loop.run();
  expect_true((trace_lines(result) == std::vector<std::string>{"M,100,book_update,1",
                                                               "M,120,book_update,2",
                                                               "M,140,trade,3"}),
              "historical replay preserves source order");
  expect_true(result.final_time_ns == 140, "basic historical replay final time");
}

void test_same_timestamp_market_events() {
  replay::EventLoop loop{{book_event(100, 10, replay::Side::Buy, 10000, 1),
                          book_event(100, 11, replay::Side::Buy, 9999, 1),
                          trade_event(100, 12, 10000, 1)}};
  const auto result = loop.run();
  expect_true((trace_lines(result) == std::vector<std::string>{"M,100,book_update,10",
                                                               "M,100,book_update,11",
                                                               "M,100,trade,12"}),
              "same timestamp market events preserve source sequence order");
}

void test_internal_event_interleaving() {
  replay::EventLoop loop{{book_event(120, 1, replay::Side::Buy, 10000, 1),
                          book_event(149, 2, replay::Side::Sell, 10001, 1),
                          book_event(151, 3, replay::Side::Buy, 9999, 1)}};
  loop.clock().advance_to(100);
  loop.schedule_internal(150, replay::InternalEvent{replay::InternalEventType::Timer, "timer"});
  const auto result = loop.run();

  expect_true((trace_lines(result) == std::vector<std::string>{"M,120,book_update,1",
                                                               "M,149,book_update,2",
                                                               "I,150,timer,0,timer",
                                                               "M,151,book_update,3"}),
              "internal event interleaves between historical events");
}

void test_market_precedes_internal_at_same_timestamp() {
  replay::EventLoop loop{{book_event(150, 1, replay::Side::Buy, 10000, 1)}};
  loop.schedule_internal(150, replay::InternalEvent{replay::InternalEventType::Timer, "same"});
  const auto result = loop.run();

  expect_true((trace_lines(result) == std::vector<std::string>{"M,150,book_update,1",
                                                               "I,150,timer,0,same"}),
              "market event precedes internal event at the same timestamp");
}

void test_multiple_internal_events_at_same_timestamp() {
  for (int iteration = 0; iteration < 25; ++iteration) {
    replay::EventLoop loop{{}};
    loop.schedule_internal(200, replay::InternalEvent{replay::InternalEventType::User, "A"});
    loop.schedule_internal(200, replay::InternalEvent{replay::InternalEventType::User, "B"});
    loop.schedule_internal(200, replay::InternalEvent{replay::InternalEventType::User, "C"});
    const auto result = loop.run();

    expect_true((trace_lines(result) == std::vector<std::string>{"I,200,user,0,A",
                                                                 "I,200,user,1,B",
                                                                 "I,200,user,2,C"}),
                "internal events at same timestamp preserve insertion order");
  }
}

void test_schedule_in_past_and_at_current_time() {
  replay::EventLoop past_loop{{}};
  past_loop.clock().advance_to(200);
  expect_throws<std::invalid_argument>(
      [&past_loop] { past_loop.schedule_internal(199, replay::InternalEvent{replay::InternalEventType::Timer, "past"}); },
      "scheduler rejects event in the past");

  replay::EventLoop current_loop{{}};
  current_loop.clock().advance_to(200);
  current_loop.schedule_internal(200, replay::InternalEvent{replay::InternalEventType::Timer, "now"});
  const auto result = current_loop.run();
  expect_true((trace_lines(result) == std::vector<std::string>{"I,200,timer,0,now"}),
              "scheduler accepts event at current time");
  expect_true(result.final_time_ns == 200, "current-time scheduled event terminates cleanly");
}

void test_empty_and_internal_only_replay() {
  replay::EventLoop empty_loop{{}};
  const auto empty_result = empty_loop.run();
  expect_true(empty_result.processed_event_count == 0, "empty replay processes zero events");
  expect_true(empty_result.final_time_ns == 0, "empty replay final time remains initial zero");
  expect_true(empty_result.trace.empty(), "empty replay trace is empty");

  replay::EventLoop internal_loop{{}};
  internal_loop.schedule_internal(300, replay::InternalEvent{replay::InternalEventType::User, "late"});
  internal_loop.schedule_internal(100, replay::InternalEvent{replay::InternalEventType::Timer, "early"});
  internal_loop.schedule_internal(300, replay::InternalEvent{replay::InternalEventType::CancelArrival, "cancel"});
  const auto internal_result = internal_loop.run();
  expect_true((trace_lines(internal_result) == std::vector<std::string>{"I,100,timer,1,early",
                                                                        "I,300,user,0,late",
                                                                        "I,300,cancel_arrival,2,cancel"}),
              "internal-only replay orders by timestamp then insertion sequence");
}

void test_book_update_integration_matches_direct_replay() {
  const replay::FeedParserConfig config{.tick_size = "0.01", .price_format = replay::PriceFieldFormat::Ticks};
  const auto feed = replay::load_book_updates_csv("tests/golden/order_book_updates.csv", config);

  replay::OrderBook direct_book;
  for (const auto& event : feed.events()) {
    direct_book.apply(event.book_update());
  }

  replay::OrderBook loop_book;
  replay::EventLoop loop{feed.events()};
  const auto result = loop.run(replay::EventLoopHandlers{.order_book = &loop_book});
  expect_true(result.processed_event_count == feed.size(), "book integration processes all golden events");
  expect_true(loop_book.best_bid() == direct_book.best_bid(), "loop book best bid matches direct replay");
  expect_true(loop_book.best_ask() == direct_book.best_ask(), "loop book best ask matches direct replay");
  expect_true(loop_book.bid_level_count() == direct_book.bid_level_count(), "loop bid count matches direct replay");
  expect_true(loop_book.ask_level_count() == direct_book.ask_level_count(), "loop ask count matches direct replay");
  expect_true(loop_book.state_hash() == direct_book.state_hash(), "loop book hash matches direct replay");
}

void test_trade_dispatch_does_not_mutate_book() {
  replay::OrderBook book;
  book.apply(replay::BookUpdateEvent{replay::EventKey{1, 1}, replay::Side::Buy, 10000, 5});
  const auto before_hash = book.state_hash();

  replay::EventLoop loop{{trade_event(100, 1, 10000, 2), trade_event(101, 2, 10001, 3)}};
  const auto result = loop.run(replay::EventLoopHandlers{.order_book = &book});

  expect_true((trace_lines(result) == std::vector<std::string>{"M,100,trade,1", "M,101,trade,2"}),
              "trade events appear in trace with timestamp and sequence");
  expect_true(book.state_hash() == before_hash, "trade dispatch does not invent book mutation");
}

void test_deterministic_trace() {
  std::optional<std::string> expected_trace;
  std::optional<std::uint64_t> expected_trace_hash;
  std::optional<replay::TimestampNs> expected_final_time;
  std::optional<std::uint64_t> expected_book_hash;

  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::OrderBook book;
    replay::EventLoop loop{{book_event(100, 1, replay::Side::Buy, 10000, 5),
                            trade_event(100, 2, 10000, 1),
                            book_event(150, 3, replay::Side::Sell, 10001, 4),
                            book_event(175, 4, replay::Side::Buy, 9999, 2)}};
    loop.schedule_internal(150, replay::InternalEvent{replay::InternalEventType::Timer, "at150"});
    loop.schedule_internal(100, replay::InternalEvent{replay::InternalEventType::User, "at100"});
    loop.schedule_internal(125, replay::InternalEvent{replay::InternalEventType::OrderArrival, "at125"});
    const auto result = loop.run(replay::EventLoopHandlers{.order_book = &book});

    const auto trace = result.canonical_trace();
    const auto hash = result.trace_hash();
    const auto book_hash = book.state_hash();

    if (!expected_trace.has_value()) {
      expected_trace = trace;
      expected_trace_hash = hash;
      expected_final_time = result.final_time_ns;
      expected_book_hash = book_hash;
    } else {
      expect_true(trace == expected_trace, "deterministic trace repeatability");
      expect_true(hash == expected_trace_hash, "deterministic trace hash repeatability");
      expect_true(result.final_time_ns == expected_final_time, "deterministic final time repeatability");
      expect_true(book_hash == expected_book_hash, "deterministic book hash repeatability");
    }
  }
}

void test_canonical_trace_fixture_exact_bytes_and_hash() {
  replay::OrderBook book;
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Buy, 10000, 5),
                          trade_event(100, 2, 10000, 1),
                          book_event(150, 3, replay::Side::Sell, 10001, 4),
                          book_event(175, 4, replay::Side::Buy, 9999, 2)}};
  loop.schedule_internal(150, replay::InternalEvent{replay::InternalEventType::Timer, "at150"});
  loop.schedule_internal(100, replay::InternalEvent{replay::InternalEventType::User, "at100"});
  loop.schedule_internal(125, replay::InternalEvent{replay::InternalEventType::OrderArrival, "at125"});

  const auto result = loop.run(replay::EventLoopHandlers{.order_book = &book});
  const std::string expected_trace =
      "M,100,book_update,1\n"
      "M,100,trade,2\n"
      "I,100,user,1,at100\n"
      "I,125,order_arrival,2,at125\n"
      "M,150,book_update,3\n"
      "I,150,timer,0,at150\n"
      "M,175,book_update,4\n";

  expect_true(result.canonical_trace() == expected_trace, "canonical trace fixture exact bytes");
  expect_true(result.trace_hash() == 0x0e1151708630d39aULL, "canonical trace fixture hash unchanged");
}

void test_no_lookahead_causality() {
  replay::OrderBook book;
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Buy, 10000, 5),
                          book_event(200, 2, replay::Side::Sell, 10001, 4)}};

  bool checked_first_event = false;
  bool checked_second_event = false;
  const auto result = loop.run(replay::EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&book, &checked_first_event, &checked_second_event](const replay::MarketEvent& event,
                                                               replay::EventLoop&) {
            if (event.key().timestamp_ns == 100) {
              checked_first_event = true;
              expect_true(book.best_bid() == replay::PriceLevel{10000, 5}, "current event state is visible at 100");
              expect_true(!book.best_ask().has_value(), "future event state is not visible at 100");
            }
            if (event.key().timestamp_ns == 200) {
              checked_second_event = true;
              expect_true(book.best_ask() == replay::PriceLevel{10001, 4}, "event state is visible at 200");
            }
          },
  });

  expect_true(result.final_time_ns == 200, "no-lookahead test final time");
  expect_true(checked_first_event, "no-lookahead checked first event");
  expect_true(checked_second_event, "no-lookahead checked second event");
}

void test_end_of_stream_with_pending_internal_events() {
  replay::EventLoop loop{{book_event(100, 1, replay::Side::Buy, 10000, 1)}};
  loop.schedule_internal(200, replay::InternalEvent{replay::InternalEventType::Timer, "after_end"});
  const auto result = loop.run();

  expect_true((trace_lines(result) == std::vector<std::string>{"M,100,book_update,1",
                                                               "I,200,timer,0,after_end"}),
              "loop continues after historical stream ends until scheduler is empty");
  expect_true(result.final_time_ns == 200, "pending internal event after market end determines final time");
}

void test_one_event_and_timestamp_limit_edges() {
  replay::EventLoop one_market{{book_event(10, 1, replay::Side::Buy, 10000, 1)}};
  expect_true(one_market.run().processed_event_count == 1, "one market event replay succeeds");

  replay::EventLoop one_internal{{}};
  constexpr auto near_max_time = std::numeric_limits<replay::TimestampNs>::max();
  one_internal.schedule_internal(near_max_time, replay::InternalEvent{replay::InternalEventType::Timer, "max"});
  const auto result = one_internal.run();
  expect_true(result.final_time_ns == near_max_time, "timestamp near representation limit is accepted");
  expect_true(result.processed_event_count == 1, "one internal event replay succeeds");
}

void test_schedule_current_time_from_handler() {
  replay::EventLoop loop{{book_event(200, 1, replay::Side::Buy, 10000, 1)}};
  bool scheduled = false;
  const auto result = loop.run(replay::EventLoopHandlers{
      .on_market =
          [&scheduled](const replay::MarketEvent&, replay::EventLoop& running_loop) {
            running_loop.schedule_internal(running_loop.clock().now(),
                                           replay::InternalEvent{replay::InternalEventType::Timer, "same_now"});
            scheduled = true;
          },
  });

  expect_true(scheduled, "current-time event scheduled from handler");
  expect_true((trace_lines(result) == std::vector<std::string>{"M,200,book_update,1",
                                                               "I,200,timer,0,same_now"}),
              "current-time event scheduled from handler is processed after market event");
}

}  // namespace

int main() {
  test_clock_monotonicity();
  test_basic_historical_replay();
  test_same_timestamp_market_events();
  test_internal_event_interleaving();
  test_market_precedes_internal_at_same_timestamp();
  test_multiple_internal_events_at_same_timestamp();
  test_schedule_in_past_and_at_current_time();
  test_empty_and_internal_only_replay();
  test_book_update_integration_matches_direct_replay();
  test_trade_dispatch_does_not_mutate_book();
  test_deterministic_trace();
  test_canonical_trace_fixture_exact_bytes_and_hash();
  test_no_lookahead_causality();
  test_end_of_stream_with_pending_internal_events();
  test_one_event_and_timestamp_limit_edges();
  test_schedule_current_time_from_handler();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
