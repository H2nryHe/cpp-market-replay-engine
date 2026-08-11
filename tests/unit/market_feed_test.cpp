#include "replay/market_feed.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
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

replay::NormalizedMarketFeed parse_book_text(std::string_view text) {
  std::istringstream input{std::string{text}};
  return replay::parse_book_updates_csv(input, "book_text", replay::FeedParserConfig{});
}

replay::NormalizedMarketFeed parse_trade_text(std::string_view text) {
  std::istringstream input{std::string{text}};
  return replay::parse_trades_csv(input, "trade_text", replay::FeedParserConfig{});
}

std::vector<std::string> canonical_sequence(const replay::NormalizedMarketFeed& feed) {
  std::vector<std::string> output;
  for (const auto& event : feed.events()) {
    output.push_back(replay::canonical_event_string(event));
  }
  return output;
}

void test_valid_l2_fixture() {
  const auto feed = replay::load_book_updates_csv("tests/fixtures/book_updates_valid.csv", replay::FeedParserConfig{});
  expect_true(feed.size() == 3, "valid book fixture emits three events");

  const auto& first = feed.events().at(0);
  expect_true(first.type() == replay::MarketEventType::BookUpdate, "first event is BookUpdate");
  expect_true(first.key() == replay::EventKey{1000, 1}, "first book event key matches");
  expect_true(first.book_update().side == replay::Side::Buy, "first book side matches");
  expect_true(first.book_update().price_ticks == 10025, "first book price ticks match");
  expect_true(first.book_update().quantity == 5, "first book quantity matches");

  const auto& second = feed.events().at(1);
  expect_true(second.key() == replay::EventKey{1000, 2}, "timestamp tie preserves increasing sequence");
  expect_true(second.book_update().side == replay::Side::Sell, "side alias S parses");
  expect_true(second.book_update().price_ticks == 10030, "second book price ticks match");
  expect_true(second.book_update().quantity == 0, "zero book quantity is valid");
}

void test_valid_trade_fixture() {
  const auto feed = replay::load_trades_csv("tests/fixtures/trades_valid.csv", replay::FeedParserConfig{});
  expect_true(feed.size() == 3, "valid trade fixture emits three events");

  const auto& first = feed.events().at(0);
  expect_true(first.type() == replay::MarketEventType::Trade, "first event is Trade");
  expect_true(first.key() == replay::EventKey{1000, 1}, "first trade event key matches");
  expect_true(first.trade().price_ticks == 10025, "first trade price ticks match");
  expect_true(first.trade().quantity == 2, "first trade quantity matches");
  expect_true(first.trade().aggressor_side == replay::Side::Buy, "known aggressor side parses");

  const auto& second = feed.events().at(1);
  expect_true(!second.trade().aggressor_side.has_value(), "empty aggressor side is unknown");

  const auto& third = feed.events().at(2);
  expect_true(third.trade().aggressor_side == replay::Side::Sell, "aggressor alias S parses");
}

void test_decimal_price_integrity() {
  const auto aligned = parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                       "1,1,buy,0.29,1\n");
  expect_true(aligned.events().at(0).book_update().price_ticks == 29,
              "decimal price 0.29 converts exactly without binary floating point");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,1,buy,100.005,1\n"));
      },
      "non-tick-aligned decimal price is rejected");
}

void test_ordering_policy() {
  const auto tied = parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                    "10,1,buy,100.00,1\n"
                                    "10,2,sell,100.01,1\n"
                                    "11,1,buy,99.99,2\n");
  expect_true(tied.size() == 3, "equal timestamp with increasing sequence is accepted");
  expect_true(tied.events().at(0).key() == replay::EventKey{10, 1}, "source order first key preserved");
  expect_true(tied.events().at(1).key() == replay::EventKey{10, 2}, "source order second key preserved");
  expect_true(tied.events().at(2).key() == replay::EventKey{11, 1}, "source order third key preserved");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "10,1,buy,100.00,1\n"
                                          "10,1,sell,100.01,1\n"));
      },
      "duplicate event key is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "10,2,buy,100.00,1\n"
                                          "10,1,sell,100.01,1\n"));
      },
      "equal timestamp with decreasing sequence is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "11,1,buy,100.00,1\n"
                                          "10,2,sell,100.01,1\n"));
      },
      "out-of-order timestamp is rejected");
}

void test_malformed_input() {
  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,1,buy,100.00\n"));
      },
      "missing column is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "abc,1,buy,100.00,1\n"));
      },
      "malformed integer is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,1,bid,100.00,1\n"));
      },
      "invalid side is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,1,buy,not-a-price,1\n"));
      },
      "invalid price is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,1,buy,100.00,-1\n"));
      },
      "invalid quantity is rejected");

  expect_throws<replay::ParseError>(
      [] {
        static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                          "1,,buy,100.00,1\n"));
      },
      "empty required field is rejected");
}

void test_empty_input() {
  const auto empty = parse_book_text("");
  expect_true(empty.empty(), "empty book input emits an empty feed");

  const auto only_blank_lines = parse_trade_text("\n \n\t\n");
  expect_true(only_blank_lines.empty(), "blank trade input emits an empty feed");
}

void test_repeatability() {
  const auto first = replay::load_book_updates_csv("tests/fixtures/book_updates_valid.csv", replay::FeedParserConfig{});
  const auto expected = canonical_sequence(first);
  for (int iteration = 0; iteration < 25; ++iteration) {
    const auto current =
        replay::load_book_updates_csv("tests/fixtures/book_updates_valid.csv", replay::FeedParserConfig{});
    expect_true(canonical_sequence(current) == expected, "repeated book fixture parse is identical");
  }
}

void test_tick_price_format() {
  const replay::FeedParserConfig config{.tick_size = "0.01", .price_format = replay::PriceFieldFormat::Ticks};
  std::istringstream input{"timestamp_ns,sequence_id,side,price,quantity\n"
                           "1,1,buy,10025,3\n"};
  const auto feed = replay::parse_book_updates_csv(input, "ticks_text", config);
  expect_true(feed.events().at(0).book_update().price_ticks == 10025, "integer tick price field parses");
}

void test_parse_error_context() {
  try {
    static_cast<void>(parse_book_text("timestamp_ns,sequence_id,side,price,quantity\n"
                                      "1,1,bid,100.00,1\n"));
  } catch (const replay::ParseError& error) {
    expect_true(error.source() == "book_text", "parse error includes source");
    expect_true(error.line_number() == 2, "parse error includes line number");
    expect_true(error.field() == "side", "parse error includes field");
    expect_true(error.value() == "bid", "parse error includes offending value");
    expect_true(std::string{error.what()}.find("book_text:2") != std::string::npos,
                "parse error message includes source and line");
    return;
  }

  expect_true(false, "parse error context test should throw");
}

}  // namespace

int main() {
  test_valid_l2_fixture();
  test_valid_trade_fixture();
  test_decimal_price_integrity();
  test_ordering_policy();
  test_malformed_input();
  test_empty_input();
  test_repeatability();
  test_tick_price_format();
  test_parse_error_context();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
