#include "replay/types.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
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

void test_price_conversion() {
  expect_true(replay::price_to_ticks("100.25", "0.01") == 10025, "100.25 at 0.01 tick converts to 10025");
  expect_true(replay::price_to_ticks("100.00", "0.01") == 10000, "100.00 at 0.01 tick converts to 10000");
  expect_true(replay::price_to_ticks("0.0015", "0.0005") == 3, "exact fractional tick conversion works");

  expect_true(replay::ticks_to_price(10025, "0.01") == "100.25", "10025 ticks formats as 100.25");
  expect_true(replay::ticks_to_price(10000, "0.01") == "100.00", "10000 ticks preserves tick precision");
  expect_true(replay::ticks_to_price(3, "0.0005") == "0.0015", "fractional tick formatting works");
  expect_true(replay::parse_price_ticks("10025") == 10025, "canonical price tick parser accepts integer text");
  expect_true(replay::parse_quantity("5") == 5, "canonical quantity parser accepts integer text");
}

void test_invalid_price_conversion() {
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::price_to_ticks("100.005", "0.01")); },
      "non-tick-aligned price is rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::price_to_ticks("-1.00", "0.01")); },
      "negative decimal price is rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::price_to_ticks("100.00", "0")); },
      "zero tick size is rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::price_to_ticks("100.00", "-0.01")); },
      "negative tick size is rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::price_to_ticks("100.00", "abc")); },
      "malformed tick size is rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::ticks_to_price(-1, "0.01")); },
      "negative price ticks are rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::parse_price_ticks("+1")); },
      "explicitly signed price ticks are rejected");
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::parse_quantity("1.5")); },
      "fractional quantity is rejected");
}

void test_side_parsing() {
  expect_true(replay::parse_side("buy") == replay::Side::Buy, "buy parses as Side::Buy");
  expect_true(replay::parse_side("B") == replay::Side::Buy, "B parses as Side::Buy");
  expect_true(replay::parse_side("sell") == replay::Side::Sell, "sell parses as Side::Sell");
  expect_true(replay::parse_side("S") == replay::Side::Sell, "S parses as Side::Sell");

  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::parse_side("Buy")); },
                                       "undocumented side alias is rejected");
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::parse_side("bid")); },
                                       "invalid side is rejected");
}

void test_enum_parsing() {
  expect_true(replay::parse_order_type("market") == replay::OrderType::Market, "market parses");
  expect_true(replay::parse_order_type("limit") == replay::OrderType::Limit, "limit parses");
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::parse_order_type("Market")); },
                                       "invalid order type casing is rejected");

  expect_true(replay::parse_order_status("new") == replay::OrderStatus::New, "new status parses");
  expect_true(replay::parse_order_status("pending") == replay::OrderStatus::Pending, "pending status parses");
  expect_true(replay::parse_order_status("acknowledged") == replay::OrderStatus::Acknowledged,
              "acknowledged status parses");
  expect_true(replay::parse_order_status("partially_filled") == replay::OrderStatus::PartiallyFilled,
              "partially_filled status parses");
  expect_true(replay::parse_order_status("filled") == replay::OrderStatus::Filled, "filled status parses");
  expect_true(replay::parse_order_status("canceled") == replay::OrderStatus::Canceled, "canceled status parses");
  expect_true(replay::parse_order_status("rejected") == replay::OrderStatus::Rejected, "rejected status parses");
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::parse_order_status("done")); },
                                       "invalid order status is rejected");
}

void test_event_ordering() {
  std::vector<replay::EventKey> keys{
      replay::EventKey{100, 2},
      replay::EventKey{99, 50},
      replay::EventKey{100, 1},
      replay::EventKey{101, 0},
  };

  std::sort(keys.begin(), keys.end());

  expect_true(keys[0] == replay::EventKey{99, 50}, "earlier timestamp sorts first");
  expect_true(keys[1] == replay::EventKey{100, 1}, "same timestamp sorts by lower sequence first");
  expect_true(keys[2] == replay::EventKey{100, 2}, "same timestamp sorts by higher sequence second");
  expect_true(keys[3] == replay::EventKey{101, 0}, "later timestamp sorts last");
  expect_true(replay::EventKey{100, 1} < replay::EventKey{100, 2}, "strict sequence ordering");
  expect_true(!(replay::EventKey{100, 2} < replay::EventKey{100, 1}), "reverse sequence ordering is false");
}

void test_invalid_values() {
  expect_true(replay::LatencyNs::from_nanoseconds(0).count == 0, "zero latency is valid");
  expect_true(replay::LatencyNs::from_microseconds(50).count == 50000, "microsecond latency converts to ns");
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::LatencyNs::from_nanoseconds(-1)); },
                                       "negative nanosecond latency is rejected");
  expect_throws<std::invalid_argument>([] { static_cast<void>(replay::LatencyNs::from_microseconds(-1)); },
                                       "negative microsecond latency is rejected");
  expect_throws<std::invalid_argument>([] { replay::validate_quantity(-1); }, "negative quantity is rejected");
  expect_throws<std::invalid_argument>([] { replay::validate_price_ticks(-1); }, "negative ticks are rejected");
  expect_throws<std::invalid_argument>([] { replay::validate_tick_size(""); }, "empty tick size is rejected");
  expect_throws<std::invalid_argument>([] { replay::validate_tick_size("0.00"); }, "zero tick size is rejected");
}

}  // namespace

int main() {
  test_price_conversion();
  test_invalid_price_conversion();
  test_side_parsing();
  test_enum_parsing();
  test_event_ordering();
  test_invalid_values();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
