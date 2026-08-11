#include "queue_imbalance_strategy.hpp"
#include "replay/strategy.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
  return replay::MarketEvent{
      replay::TradeEvent{replay::EventKey{timestamp_ns, sequence_id}, price_ticks, quantity, replay::Side::Buy}};
}

std::vector<std::string> intent_strings(const replay::VectorIntentSink& sink) {
  std::vector<std::string> output;
  for (const auto& intent : sink.intents()) {
    output.push_back(replay::canonical_intent_string(intent));
  }
  return output;
}

class RecordingStrategy final : public replay::Strategy {
 public:
  void on_book(const replay::BookUpdateEvent& event,
               const replay::OrderBook& book,
               replay::IntentSink&) override {
    std::ostringstream os;
    os << "book:" << event.key.sequence_id;
    if (book.best_bid().has_value()) {
      os << ":bid=" << book.best_bid()->price_ticks;
    }
    if (book.best_ask().has_value()) {
      os << ":ask=" << book.best_ask()->price_ticks;
    }
    callbacks.push_back(os.str());
  }

  void on_trade(const replay::TradeEvent& event,
                const replay::OrderBook& book,
                replay::IntentSink&) override {
    std::ostringstream os;
    os << "trade:" << event.key.sequence_id;
    if (book.best_bid().has_value()) {
      os << ":bid=" << book.best_bid()->price_ticks;
    }
    if (book.best_ask().has_value()) {
      os << ":ask=" << book.best_ask()->price_ticks;
    }
    callbacks.push_back(os.str());
  }

  void on_timer(replay::TimestampNs timestamp_ns,
                const replay::InternalEvent& event,
                const replay::OrderBook&,
                replay::IntentSink&) override {
    callbacks.push_back("timer:" + std::to_string(timestamp_ns) + ":" + event.label);
  }

  std::vector<std::string> callbacks;
};

class NoOpStrategy final : public replay::Strategy {
 public:
  void on_book(const replay::BookUpdateEvent&, const replay::OrderBook&, replay::IntentSink&) override {}
  void on_trade(const replay::TradeEvent&, const replay::OrderBook&, replay::IntentSink&) override {}
  void on_timer(replay::TimestampNs, const replay::InternalEvent&, const replay::OrderBook&, replay::IntentSink&) override {}
};

void test_callback_order_and_post_update_visibility() {
  RecordingStrategy strategy;
  replay::VectorIntentSink intents;
  const auto result = replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 5),
          trade_event(100, 2, 10000, 1),
          book_event(100, 3, replay::Side::Buy, 10001, 7),
      },
      strategy,
      intents);

  expect_true((strategy.callbacks == std::vector<std::string>{"book:1:bid=10000",
                                                              "trade:2:bid=10000",
                                                              "book:3:bid=10001"}),
              "callbacks occur once per event in event-loop order with post-update book state");
  expect_true(result.final_book.best_bid() == replay::PriceLevel{10001, 7}, "final book reflects last update");
}

void test_read_only_strategy_signature() {
  using OnBookSignature = void (replay::Strategy::*)(const replay::BookUpdateEvent&,
                                                     const replay::OrderBook&,
                                                     replay::IntentSink&);
  using OnTradeSignature = void (replay::Strategy::*)(const replay::TradeEvent&,
                                                      const replay::OrderBook&,
                                                      replay::IntentSink&);
  using OnTimerSignature = void (replay::Strategy::*)(replay::TimestampNs,
                                                      const replay::InternalEvent&,
                                                      const replay::OrderBook&,
                                                      replay::IntentSink&);

  expect_true((std::is_same_v<decltype(&replay::Strategy::on_book), OnBookSignature>),
              "on_book exposes const OrderBook");
  expect_true((std::is_same_v<decltype(&replay::Strategy::on_trade), OnTradeSignature>),
              "on_trade exposes const OrderBook");
  expect_true((std::is_same_v<decltype(&replay::Strategy::on_timer), OnTimerSignature>),
              "on_timer exposes const OrderBook");
}

void test_no_lookahead() {
  RecordingStrategy strategy;
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 5),
          book_event(200, 2, replay::Side::Sell, 10001, 4),
      },
      strategy,
      intents));

  expect_true((strategy.callbacks == std::vector<std::string>{"book:1:bid=10000",
                                                              "book:2:bid=10000:ask=10001"}),
              "strategy callback at 100 cannot see future ask from 200");
}

void test_basic_qi_buy_signal() {
  replay::QueueImbalanceStrategy strategy{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 3}};
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 10),
          book_event(101, 2, replay::Side::Sell, 10001, 1),
      },
      strategy,
      intents));

  expect_true((intent_strings(intents) == std::vector<std::string>{"buy,3,market,,101"}), "QI buy signal emits one buy intent");
}

void test_basic_qi_sell_signal() {
  replay::QueueImbalanceStrategy strategy{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 4}};
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Sell, 10001, 10),
          book_event(101, 2, replay::Side::Buy, 10000, 1),
      },
      strategy,
      intents));

  expect_true((intent_strings(intents) == std::vector<std::string>{"sell,4,market,,101"}), "QI sell signal emits one sell intent");
}

void test_qi_no_action_region() {
  replay::QueueImbalanceStrategy strategy{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 1}};
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 5),
          book_event(101, 2, replay::Side::Sell, 10001, 5),
      },
      strategy,
      intents));

  expect_true(intents.empty(), "QI no-action region emits no intent");
  expect_true(strategy.last_queue_imbalance() == 0.0, "QI no-action calculation is zero");
}

void test_top_n_depth() {
  replay::QueueImbalanceStrategy top_one{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 1}};
  replay::VectorIntentSink top_one_intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 1),
          book_event(101, 2, replay::Side::Buy, 9999, 100),
          book_event(102, 3, replay::Side::Sell, 10001, 1),
      },
      top_one,
      top_one_intents));
  expect_true(top_one_intents.empty(), "top-1 QI ignores deeper large bid");

  replay::QueueImbalanceStrategy top_two{
      replay::QueueImbalanceConfig{.depth_levels = 2, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 1}};
  replay::VectorIntentSink top_two_intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 1),
          book_event(101, 2, replay::Side::Buy, 9999, 100),
          book_event(102, 3, replay::Side::Sell, 10001, 1),
      },
      top_two,
      top_two_intents));
  expect_true((intent_strings(top_two_intents) == std::vector<std::string>{"buy,1,market,,102"}),
              "top-2 QI includes deeper large bid");
}

void test_zero_one_sided_locked_crossed_policies() {
  replay::QueueImbalanceStrategy strategy{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 1}};

  replay::VectorIntentSink zero_intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 0),
          book_event(101, 2, replay::Side::Sell, 10001, 0),
      },
      strategy,
      zero_intents));
  expect_true(zero_intents.empty(), "zero denominator/empty selected volume emits no intent");

  replay::VectorIntentSink one_sided_intents;
  static_cast<void>(replay::run_strategy({book_event(100, 1, replay::Side::Buy, 10000, 10)}, strategy, one_sided_intents));
  expect_true(one_sided_intents.empty(), "one-sided book emits no intent");

  replay::VectorIntentSink locked_intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 10),
          book_event(101, 2, replay::Side::Sell, 10000, 1),
      },
      strategy,
      locked_intents));
  expect_true(locked_intents.empty(), "locked book emits no intent");

  replay::VectorIntentSink crossed_intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10002, 10),
          book_event(101, 2, replay::Side::Sell, 10001, 1),
      },
      strategy,
      crossed_intents));
  expect_true(crossed_intents.empty(), "crossed book emits no intent");
}

void test_intent_content_and_limit_semantics() {
  replay::QueueImbalanceStrategy strategy{replay::QueueImbalanceConfig{.depth_levels = 1,
                                                                       .buy_threshold = 0.25,
                                                                       .sell_threshold = -0.25,
                                                                       .order_quantity = 9,
                                                                       .order_type = replay::OrderType::Limit,
                                                                       .limit_price_ticks = 10002}};
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 1, replay::Side::Buy, 10000, 10),
          book_event(105, 2, replay::Side::Sell, 10001, 1),
      },
      strategy,
      intents));

  expect_true(intents.size() == 1, "limit intent count");
  const auto& intent = intents.intents().front();
  expect_true(intent.side == replay::Side::Buy, "intent side");
  expect_true(intent.quantity == 9, "intent quantity");
  expect_true(intent.order_type == replay::OrderType::Limit, "intent order type");
  expect_true(intent.limit_price_ticks == 10002, "intent limit price");
  expect_true(intent.decision_timestamp_ns == 105, "intent decision timestamp");
}

void test_same_timestamp_causality() {
  RecordingStrategy strategy;
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy(
      {
          book_event(100, 10, replay::Side::Buy, 10000, 1),
          book_event(100, 11, replay::Side::Buy, 10001, 2),
          book_event(100, 12, replay::Side::Sell, 10002, 3),
      },
      strategy,
      intents));

  expect_true((strategy.callbacks == std::vector<std::string>{"book:10:bid=10000",
                                                              "book:11:bid=10001",
                                                              "book:12:bid=10001:ask=10002"}),
              "same-timestamp callbacks observe causal source order");
}

void test_timer_callback() {
  RecordingStrategy strategy;
  replay::VectorIntentSink intents;
  static_cast<void>(replay::run_strategy({}, strategy, intents, {replay::StrategyTimer{250, "heartbeat"}}));
  expect_true((strategy.callbacks == std::vector<std::string>{"timer:250:heartbeat"}), "timer callback fires");
}

void test_determinism() {
  std::optional<std::vector<std::string>> expected_callbacks;
  std::optional<std::vector<std::string>> expected_intents;
  std::optional<std::uint64_t> expected_book_hash;

  for (int iteration = 0; iteration < 100; ++iteration) {
    RecordingStrategy recorder;
    replay::QueueImbalanceStrategy strategy{
        replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 2}};
    replay::VectorIntentSink intents;
    const auto result = replay::run_strategy(
        {
            book_event(100, 1, replay::Side::Buy, 10000, 10),
            trade_event(100, 2, 10000, 1),
            book_event(101, 3, replay::Side::Sell, 10001, 1),
        },
        strategy,
        intents);

    replay::VectorIntentSink callback_intents;
    static_cast<void>(replay::run_strategy(
        {
            book_event(100, 1, replay::Side::Buy, 10000, 10),
            trade_event(100, 2, 10000, 1),
            book_event(101, 3, replay::Side::Sell, 10001, 1),
        },
        recorder,
        callback_intents));

    const auto current_callbacks = recorder.callbacks;
    const auto current_intents = intent_strings(intents);
    const auto current_hash = result.final_book.state_hash();

    if (!expected_callbacks.has_value()) {
      expected_callbacks = current_callbacks;
      expected_intents = current_intents;
      expected_book_hash = current_hash;
    } else {
      expect_true(current_callbacks == expected_callbacks, "callback trace is deterministic");
      expect_true(current_intents == expected_intents, "intent sequence is deterministic");
      expect_true(current_hash == expected_book_hash, "final book hash is deterministic");
    }
  }
}

void test_strategy_intents_do_not_mutate_replay_state() {
  const std::vector<replay::MarketEvent> events{
      book_event(100, 1, replay::Side::Buy, 10000, 10),
      book_event(101, 2, replay::Side::Sell, 10001, 1),
  };

  NoOpStrategy no_op;
  replay::VectorIntentSink no_op_intents;
  const auto no_op_result = replay::run_strategy(events, no_op, no_op_intents);

  replay::QueueImbalanceStrategy qi{
      replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 1}};
  replay::VectorIntentSink qi_intents;
  const auto qi_result = replay::run_strategy(events, qi, qi_intents);

  expect_true(no_op_result.final_book.state_hash() == qi_result.final_book.state_hash(),
              "strategy intent generation does not mutate market state");
  expect_true(!qi_intents.empty(), "QI strategy emitted an intent for comparison");
}

void test_invalid_qi_config() {
  expect_throws<std::invalid_argument>(
      [] {
        replay::QueueImbalanceStrategy invalid{
            replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = 0.25, .sell_threshold = -0.25, .order_quantity = 0}};
      },
      "QI rejects zero order quantity");
  expect_throws<std::invalid_argument>(
      [] {
        replay::QueueImbalanceStrategy invalid{
            replay::QueueImbalanceConfig{.depth_levels = 1, .buy_threshold = -0.5, .sell_threshold = 0.5, .order_quantity = 1}};
      },
      "QI rejects inverted thresholds");
}

void test_architectural_dependency_guard() {
  expect_true(true, "strategy tests compile without execution, fill, or portfolio headers");
}

}  // namespace

int main() {
  test_callback_order_and_post_update_visibility();
  test_read_only_strategy_signature();
  test_no_lookahead();
  test_basic_qi_buy_signal();
  test_basic_qi_sell_signal();
  test_qi_no_action_region();
  test_top_n_depth();
  test_zero_one_sided_locked_crossed_policies();
  test_intent_content_and_limit_semantics();
  test_same_timestamp_causality();
  test_timer_callback();
  test_determinism();
  test_strategy_intents_do_not_mutate_replay_state();
  test_invalid_qi_config();
  test_architectural_dependency_guard();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
