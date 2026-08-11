#include "replay/execution_simulator.hpp"

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

replay::Quantity fill_quantity_sum(const std::vector<replay::Fill>& fills) {
  replay::Quantity total = 0;
  for (const auto& fill : fills) {
    total += fill.quantity;
  }
  return total;
}

void verify_fill_conservation(const replay::ExecutionResult& result, std::string_view message) {
  expect_true(fill_quantity_sum(result.fills) == result.order.filled_quantity(), std::string{message} + ": fill sum");
  expect_true(result.order.filled_quantity() + result.order.remaining_quantity() == result.order.original_quantity(),
              std::string{message} + ": filled plus remaining");
  expect_true(result.order.filled_quantity() <= result.order.original_quantity(), std::string{message} + ": no overfill");
  expect_true(result.order.remaining_quantity() >= 0, std::string{message} + ": non-negative remaining");
}

void test_deterministic_order_ids() {
  replay::OrderFactory first;
  const auto a = first.create_order(replay::Side::Buy, replay::OrderType::Market, 1, 100);
  const auto b = first.create_order(replay::Side::Sell, replay::OrderType::Market, 2, 101);
  const auto c = first.create_order(replay::Side::Buy, replay::OrderType::Limit, 3, 102, 10000);
  expect_true(a.order_id() == 1 && b.order_id() == 2 && c.order_id() == 3, "order IDs are monotonic");

  replay::OrderFactory second;
  const auto repeated_a = second.create_order(replay::Side::Buy, replay::OrderType::Market, 1, 100);
  const auto repeated_b = second.create_order(replay::Side::Sell, replay::OrderType::Market, 2, 101);
  expect_true(repeated_a.order_id() == 1 && repeated_b.order_id() == 2, "order IDs repeat for identical factory sequence");
}

void test_valid_lifecycle() {
  replay::Order order{1, replay::Side::Buy, replay::OrderType::Market, 3, 100, 100};
  order.transition_to(replay::OrderStatus::Pending);
  order.transition_to(replay::OrderStatus::Acknowledged);
  order.record_fill_quantity(3);
  order.transition_to(replay::OrderStatus::Filled);

  expect_true((status_names(order) == std::vector<std::string>{"new", "pending", "acknowledged", "filled"}),
              "valid market lifecycle records exact statuses");
  expect_true(order.filled_quantity() == 3 && order.remaining_quantity() == 0, "valid lifecycle filled quantities");
}

void test_invalid_lifecycle() {
  replay::Order filled{1, replay::Side::Buy, replay::OrderType::Market, 1, 100, 100};
  filled.transition_to(replay::OrderStatus::Pending);
  filled.transition_to(replay::OrderStatus::Acknowledged);
  filled.record_fill_quantity(1);
  filled.transition_to(replay::OrderStatus::Filled);
  expect_throws<std::invalid_argument>([&filled] { filled.transition_to(replay::OrderStatus::Pending); },
                                       "Filled -> Pending rejected");

  replay::Order canceled{2, replay::Side::Buy, replay::OrderType::Market, 2, 100, 100};
  canceled.transition_to(replay::OrderStatus::Pending);
  canceled.transition_to(replay::OrderStatus::Acknowledged);
  canceled.transition_to(replay::OrderStatus::Canceled);
  expect_throws<std::invalid_argument>([&canceled] { canceled.transition_to(replay::OrderStatus::Filled); },
                                       "Canceled -> Filled rejected");

  replay::Order rejected{3, replay::Side::Buy, replay::OrderType::Market, 2, 100, 100};
  rejected.transition_to(replay::OrderStatus::Pending);
  rejected.transition_to(replay::OrderStatus::Rejected);
  expect_throws<std::invalid_argument>([&rejected] { rejected.transition_to(replay::OrderStatus::Filled); },
                                       "Rejected -> Filled rejected");
}

void test_one_level_buy_market_fill() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 5)});
  replay::ExecutionSimulator simulator;
  auto order = simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 3, 200);
  const auto result = simulator.execute_order(order, book);

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10001,3,200,0"}),
              "one-level buy fill");
  expect_true(result.order.filled_quantity() == 3, "one-level buy filled quantity");
  expect_true(result.order.remaining_quantity() == 0, "one-level buy remaining quantity");
  expect_true(result.order.status() == replay::OrderStatus::Filled, "one-level buy filled status");
  verify_fill_conservation(result, "one-level buy");
}

void test_one_level_sell_market_fill() {
  const auto book = book_from({update(replay::Side::Buy, 9999, 5)});
  replay::ExecutionSimulator simulator;
  auto order = simulator.create_order(replay::Side::Sell, replay::OrderType::Market, 4, 200);
  const auto result = simulator.execute_order(order, book);

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,sell,9999,4,200,0"}),
              "one-level sell fill");
  expect_true(result.order.status() == replay::OrderStatus::Filled, "one-level sell filled status");
  verify_fill_conservation(result, "one-level sell");
}

void test_multi_level_buy_sweep() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 2),
                               update(replay::Side::Sell, 10002, 3),
                               update(replay::Side::Sell, 10003, 5)});
  replay::ExecutionSimulator simulator;
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 4, 300), book);

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,buy,10001,2,300,0",
                                                                      "2,1,buy,10002,2,300,0"}),
              "multi-level buy walks asks low to high");
  expect_true(result.order.filled_quantity() == 4 && result.order.remaining_quantity() == 0, "multi-level buy quantities");
  verify_fill_conservation(result, "multi-level buy");
}

void test_multi_level_sell_sweep() {
  const auto book = book_from({update(replay::Side::Buy, 10000, 2),
                               update(replay::Side::Buy, 9999, 3),
                               update(replay::Side::Buy, 9998, 5)});
  replay::ExecutionSimulator simulator;
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Sell, replay::OrderType::Market, 4, 300), book);

  expect_true((fill_strings(result.fills) == std::vector<std::string>{"1,1,sell,10000,2,300,0",
                                                                      "2,1,sell,9999,2,300,0"}),
              "multi-level sell walks bids high to low");
  verify_fill_conservation(result, "multi-level sell");
}

void test_insufficient_liquidity() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3)});
  replay::ExecutionSimulator simulator;
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 8, 400), book);

  expect_true(result.order.filled_quantity() == 5, "insufficient liquidity filled quantity");
  expect_true(result.order.remaining_quantity() == 3, "insufficient liquidity remaining quantity");
  expect_true(result.order.status() == replay::OrderStatus::Canceled, "insufficient liquidity terminal status");
  expect_true((status_names(result.order) ==
               std::vector<std::string>{"new", "pending", "acknowledged", "partially_filled", "canceled"}),
              "insufficient liquidity cancels unfilled market remainder");
  verify_fill_conservation(result, "insufficient liquidity");
}

void test_empty_and_one_sided_books() {
  replay::ExecutionSimulator simulator;

  const replay::OrderBook empty;
  const auto no_asks = simulator.execute_order(
      simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 1, 500), empty);
  expect_true(no_asks.fills.empty() && no_asks.order.status() == replay::OrderStatus::Rejected,
              "buy market with no asks is rejected without fills");

  const auto bids_only_book = book_from({update(replay::Side::Buy, 10000, 5)});
  const auto buy_bids_only = simulator.execute_order(
      simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 1, 501), bids_only_book);
  expect_true(buy_bids_only.fills.empty() && buy_bids_only.order.status() == replay::OrderStatus::Rejected,
              "buy market when only bids exist has no executable liquidity");

  const auto asks_only_book = book_from({update(replay::Side::Sell, 10001, 5)});
  const auto sell_asks_only = simulator.execute_order(
      simulator.create_order(replay::Side::Sell, replay::OrderType::Market, 1, 502), asks_only_book);
  expect_true(sell_asks_only.fills.empty() && sell_asks_only.order.status() == replay::OrderStatus::Rejected,
              "sell market when only asks exist has no executable liquidity");
}

void test_historical_book_immutability() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3)});
  const auto before_state = book.canonical_state();
  const auto before_hash = book.state_hash();

  replay::ExecutionSimulator simulator;
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 4, 600), book);

  expect_true(result.fills.size() == 2, "immutability test still generates fills");
  expect_true(book.canonical_state() == before_state, "execution does not mutate historical book state");
  expect_true(book.state_hash() == before_hash, "execution does not mutate historical book hash");
  expect_true(book.ask_quantity_at(10001) == 2 && book.ask_quantity_at(10002) == 3,
              "historical ask quantities remain unchanged");
}

void test_no_overfill() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 100)});
  replay::ExecutionSimulator simulator;
  const auto small = simulator.execute_order(
      simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 7, 700), book);
  expect_true(small.order.filled_quantity() == 7 && fill_quantity_sum(small.fills) == 7, "small order does not overfill");

  const auto large = simulator.execute_order(
      simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 150, 701), book);
  expect_true(large.order.filled_quantity() == 100 && large.order.remaining_quantity() == 50,
              "large order fills only visible liquidity");
  verify_fill_conservation(small, "small no-overfill");
  verify_fill_conservation(large, "large no-overfill");
}

void test_fee_model() {
  const replay::FeeModel model{100};
  expect_true(model.fee_rate_ppm() == 100, "fee rate is stored in ppm");
  expect_true(model.calculate_fee(10000, 10) == 10, "fee calculation exact hand example");
  expect_true(replay::FeeModel{1}.calculate_fee(500000, 1) == 1, "fee calculation rounds half up");
  expect_throws<std::invalid_argument>([] { replay::FeeModel invalid{-1}; }, "negative fee rate rejected");
  expect_throws<std::invalid_argument>([] { replay::FeeModel invalid{1'000'001}; },
                                       "fee rate above 100 percent rejected");
}

void test_multi_fill_fee_consistency() {
  const auto book = book_from({update(replay::Side::Sell, 10000, 2), update(replay::Side::Sell, 20000, 1)});
  replay::ExecutionSimulator simulator{replay::ExecutionConfig{.fee_model = replay::FeeModel{100}}};
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 3, 800), book);

  expect_true(result.fills.size() == 2, "multi-fill fee test fill count");
  expect_true(result.fills[0].fee_amount == 2, "fee is calculated per fill at first level");
  expect_true(result.fills[1].fee_amount == 2, "fee is calculated per fill at second level");
}

void test_limit_order_phase_boundary() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 5)});
  replay::ExecutionSimulator simulator;
  auto limit = simulator.create_order(replay::Side::Buy, replay::OrderType::Limit, 5, 900, 10001);
  const auto result = simulator.execute_order(limit, book);

  expect_true(result.fills.empty(), "Phase 6 limit order produces no fills");
  expect_true(result.order.status() == replay::OrderStatus::Rejected, "Phase 6 limit order is explicitly rejected");
  expect_true((status_names(result.order) == std::vector<std::string>{"new", "pending", "rejected"}),
              "Phase 6 limit order rejected lifecycle");
}

void test_order_intent_conversion() {
  const replay::OrderIntent intent{.side = replay::Side::Sell,
                                   .quantity = 6,
                                   .order_type = replay::OrderType::Market,
                                   .limit_price_ticks = std::nullopt,
                                   .decision_timestamp_ns = 1234};
  replay::ExecutionSimulator simulator;
  const auto order = simulator.create_order_from_intent(intent);

  expect_true(order.order_id() == 1, "intent conversion deterministic order ID");
  expect_true(order.side() == replay::Side::Sell, "intent conversion side");
  expect_true(order.original_quantity() == 6, "intent conversion quantity");
  expect_true(order.order_type() == replay::OrderType::Market, "intent conversion order type");
  expect_true(order.submit_timestamp_ns() == 1234, "intent conversion submit timestamp");
  expect_true(order.exchange_arrival_timestamp_ns() == 1234, "intent conversion immediate arrival timestamp");
  expect_true(order.status() == replay::OrderStatus::New && order.filled_quantity() == 0,
              "intent conversion does not execute");
}

void test_determinism() {
  std::optional<std::vector<std::string>> expected_fills;
  std::optional<std::string> expected_order;
  std::optional<std::vector<std::string>> expected_statuses;

  const auto book = book_from({update(replay::Side::Sell, 10001, 2),
                               update(replay::Side::Sell, 10002, 3),
                               update(replay::Side::Sell, 10003, 5)});

  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::ExecutionSimulator simulator{replay::ExecutionConfig{.fee_model = replay::FeeModel{100}}};
    const auto result =
        simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 4, 1000), book);

    const auto fills = fill_strings(result.fills);
    const auto order = replay::canonical_order_string(result.order);
    const auto statuses = status_names(result.order);
    if (!expected_fills.has_value()) {
      expected_fills = fills;
      expected_order = order;
      expected_statuses = statuses;
    } else {
      expect_true(fills == expected_fills, "deterministic fill sequence");
      expect_true(order == expected_order, "deterministic order outcome");
      expect_true(statuses == expected_statuses, "deterministic lifecycle");
    }
  }
}

void test_invalid_order_values_and_fill_quantity() {
  expect_throws<std::invalid_argument>(
      [] { replay::Order invalid{0, replay::Side::Buy, replay::OrderType::Market, 1, 1, 1}; },
      "zero order ID rejected");
  expect_throws<std::invalid_argument>(
      [] { replay::Order invalid{1, replay::Side::Buy, replay::OrderType::Market, 0, 1, 1}; },
      "zero order quantity rejected");
  expect_throws<std::invalid_argument>(
      [] { replay::Order invalid{1, replay::Side::Buy, replay::OrderType::Limit, 1, 1, 1}; },
      "limit order without limit price rejected");

  replay::Order order{1, replay::Side::Buy, replay::OrderType::Market, 1, 1, 1};
  expect_throws<std::invalid_argument>([&order] { order.record_fill_quantity(2); }, "overfill rejected explicitly");
}

}  // namespace

int main() {
  test_deterministic_order_ids();
  test_valid_lifecycle();
  test_invalid_lifecycle();
  test_one_level_buy_market_fill();
  test_one_level_sell_market_fill();
  test_multi_level_buy_sweep();
  test_multi_level_sell_sweep();
  test_insufficient_liquidity();
  test_empty_and_one_sided_books();
  test_historical_book_immutability();
  test_no_overfill();
  test_fee_model();
  test_multi_fill_fee_consistency();
  test_limit_order_phase_boundary();
  test_order_intent_conversion();
  test_determinism();
  test_invalid_order_values_and_fill_quantity();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
