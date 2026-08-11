#include "replay/order_book.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
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

replay::BookUpdateEvent update(replay::Side side, replay::PriceTicks price_ticks, replay::Quantity quantity) {
  return replay::BookUpdateEvent{replay::EventKey{}, side, price_ticks, quantity};
}

void apply_sequence(replay::OrderBook& book, const std::vector<replay::BookUpdateEvent>& updates) {
  for (const auto& event : updates) {
    book.apply(event);
  }
}

bool contains_level(const std::vector<replay::PriceLevel>& levels, replay::PriceLevel expected) {
  for (const auto& level : levels) {
    if (level == expected) {
      return true;
    }
  }
  return false;
}

void verify_invariants(const replay::OrderBook& book) {
  expect_true(!book.has_zero_quantity_levels(), "book stores no zero-quantity levels");
  expect_true(!book.has_negative_quantity_levels(), "book stores no negative-quantity levels");
  expect_true(book.bid_ordering_is_strict(), "bid levels are strictly ordered best to worst");
  expect_true(book.ask_ordering_is_strict(), "ask levels are strictly ordered best to worst");

  const auto bids = book.top_bids(book.bid_level_count());
  const auto asks = book.top_asks(book.ask_level_count());
  if (!bids.empty()) {
    expect_true(book.best_bid() == bids.front(), "best bid equals maximum bid level");
  }
  if (!asks.empty()) {
    expect_true(book.best_ask() == asks.front(), "best ask equals minimum ask level");
  }
}

void test_basic_insert() {
  replay::OrderBook book;
  apply_sequence(book,
                 {
                     update(replay::Side::Buy, 10000, 5),
                     update(replay::Side::Buy, 9999, 3),
                     update(replay::Side::Sell, 10001, 4),
                     update(replay::Side::Sell, 10002, 8),
                 });

  expect_true(book.best_bid() == replay::PriceLevel{10000, 5}, "best bid after basic insert");
  expect_true(book.best_ask() == replay::PriceLevel{10001, 4}, "best ask after basic insert");
  expect_true(book.bid_quantity_at(10000) == 5, "bid depth lookup for 10000");
  expect_true(book.ask_quantity_at(10001) == 4, "ask depth lookup for 10001");
  expect_true(book.bid_level_count() == 2, "basic insert bid level count");
  expect_true(book.ask_level_count() == 2, "basic insert ask level count");
  verify_invariants(book);
}

void test_price_priority() {
  replay::OrderBook book;
  apply_sequence(book,
                 {
                     update(replay::Side::Buy, 9998, 1),
                     update(replay::Side::Buy, 10001, 2),
                     update(replay::Side::Buy, 9999, 3),
                     update(replay::Side::Sell, 10005, 4),
                     update(replay::Side::Sell, 10002, 5),
                     update(replay::Side::Sell, 10003, 6),
                 });

  const auto bids = book.top_bids(10);
  expect_true(bids.size() == 3, "all bid levels returned");
  expect_true(bids[0] == replay::PriceLevel{10001, 2}, "highest bid first");
  expect_true(bids[1] == replay::PriceLevel{9999, 3}, "middle bid second");
  expect_true(bids[2] == replay::PriceLevel{9998, 1}, "lowest bid third");

  const auto asks = book.top_asks(10);
  expect_true(asks.size() == 3, "all ask levels returned");
  expect_true(asks[0] == replay::PriceLevel{10002, 5}, "lowest ask first");
  expect_true(asks[1] == replay::PriceLevel{10003, 6}, "middle ask second");
  expect_true(asks[2] == replay::PriceLevel{10005, 4}, "highest ask third");
  verify_invariants(book);
}

void test_update_replace() {
  replay::OrderBook book;
  book.apply(update(replay::Side::Buy, 10000, 5));
  book.apply(update(replay::Side::Buy, 10000, 9));

  expect_true(book.bid_quantity_at(10000) == 9, "update replaces quantity rather than adding");
  expect_true(book.best_bid() == replay::PriceLevel{10000, 9}, "best bid reflects replacement");
  verify_invariants(book);
}

void test_delete() {
  replay::OrderBook book;
  book.apply(update(replay::Side::Sell, 10001, 4));
  book.apply(update(replay::Side::Sell, 10001, 0));

  expect_true(!book.ask_quantity_at(10001).has_value(), "ask level removed by zero quantity");
  expect_true(!book.best_ask().has_value(), "best ask empty after delete");
  expect_true(book.ask_level_count() == 0, "ask count zero after delete");
  verify_invariants(book);
}

void test_delete_nonexistent_level() {
  replay::OrderBook book;
  book.apply(update(replay::Side::Buy, 10000, 5));
  book.apply(update(replay::Side::Sell, 10001, 0));

  expect_true(book.best_bid() == replay::PriceLevel{10000, 5}, "delete absent ask is idempotent no-op");
  expect_true(book.ask_level_count() == 0, "delete absent ask does not create a level");
  verify_invariants(book);
}

void test_empty_book() {
  const replay::OrderBook book;
  expect_true(book.empty(), "new book is empty");
  expect_true(!book.best_bid().has_value(), "empty book has no best bid");
  expect_true(!book.best_ask().has_value(), "empty book has no best ask");
  expect_true(!book.spread_ticks().has_value(), "empty book has no spread");
  expect_true(!book.mid_price_x2_ticks().has_value(), "empty book has no mid");
  expect_true(book.top_bids(5).empty(), "empty book has no top bids");
  expect_true(book.top_asks(5).empty(), "empty book has no top asks");
}

void test_one_sided_book() {
  replay::OrderBook bids_only;
  bids_only.apply(update(replay::Side::Buy, 10000, 5));
  expect_true(bids_only.best_bid() == replay::PriceLevel{10000, 5}, "one-sided bid best exists");
  expect_true(!bids_only.best_ask().has_value(), "one-sided bid book has no ask");
  expect_true(!bids_only.spread_ticks().has_value(), "one-sided bid book has no spread");
  expect_true(!bids_only.mid_price_x2_ticks().has_value(), "one-sided bid book has no mid");
  expect_true(!bids_only.is_valid_two_sided_market(), "one-sided bid book is not valid two-sided market");

  replay::OrderBook asks_only;
  asks_only.apply(update(replay::Side::Sell, 10001, 4));
  expect_true(!asks_only.best_bid().has_value(), "one-sided ask book has no bid");
  expect_true(asks_only.best_ask() == replay::PriceLevel{10001, 4}, "one-sided ask best exists");
  expect_true(!asks_only.spread_ticks().has_value(), "one-sided ask book has no spread");
  expect_true(!asks_only.mid_price_x2_ticks().has_value(), "one-sided ask book has no mid");
}

void test_spread_and_mid() {
  replay::OrderBook book;
  book.apply(update(replay::Side::Buy, 10000, 5));
  book.apply(update(replay::Side::Sell, 10002, 4));

  expect_true(book.spread_ticks() == 2, "spread is ask minus bid in ticks");
  expect_true(book.mid_price_x2_ticks() == 20002, "mid x2 is bid plus ask");

  replay::OrderBook half_tick;
  half_tick.apply(update(replay::Side::Buy, 10000, 5));
  half_tick.apply(update(replay::Side::Sell, 10001, 4));
  expect_true(half_tick.mid_price_x2_ticks() == 20001, "half-tick midpoint is represented exactly");
}

void test_top_n() {
  replay::OrderBook book;
  apply_sequence(book,
                 {
                     update(replay::Side::Buy, 10000, 1),
                     update(replay::Side::Buy, 9999, 2),
                     update(replay::Side::Buy, 9998, 3),
                     update(replay::Side::Sell, 10001, 4),
                     update(replay::Side::Sell, 10002, 5),
                 });

  const auto top_two = book.top_bids(2);
  expect_true(top_two.size() == 2, "top-N returns exactly N when enough levels exist");
  expect_true(top_two[0] == replay::PriceLevel{10000, 1}, "top-N bid first");
  expect_true(top_two[1] == replay::PriceLevel{9999, 2}, "top-N bid second");
  expect_true(book.top_asks(5).size() == 2, "top-N returns all available when fewer than N exist");
  expect_true(book.top_bids(0).empty(), "top-N with N=0 returns empty");
}

void test_locked_and_crossed_books() {
  replay::OrderBook locked;
  locked.apply(update(replay::Side::Buy, 10000, 5));
  locked.apply(update(replay::Side::Sell, 10000, 4));
  expect_true(locked.is_locked(), "locked book is detectable");
  expect_true(!locked.is_crossed(), "locked book is not crossed");
  expect_true(!locked.is_valid_two_sided_market(), "locked book is not valid two-sided market");
  expect_true(locked.spread_ticks() == 0, "locked spread is zero");

  replay::OrderBook crossed;
  crossed.apply(update(replay::Side::Buy, 10005, 5));
  crossed.apply(update(replay::Side::Sell, 10001, 4));
  expect_true(crossed.best_bid() == replay::PriceLevel{10005, 5}, "crossed best bid preserved");
  expect_true(crossed.best_ask() == replay::PriceLevel{10001, 4}, "crossed best ask preserved");
  expect_true(crossed.is_crossed(), "crossed book is detectable");
  expect_true(!crossed.is_locked(), "crossed book is not locked");
  expect_true(!crossed.is_valid_two_sided_market(), "crossed book is not valid two-sided market");
  expect_true(crossed.spread_ticks() == -4, "crossed spread remains visible as negative");
}

void test_extreme_values_and_validation() {
  replay::OrderBook book;
  constexpr auto max_price = std::numeric_limits<replay::PriceTicks>::max();
  constexpr auto max_quantity = std::numeric_limits<replay::Quantity>::max();
  book.apply(update(replay::Side::Buy, max_price, max_quantity));
  expect_true(book.best_bid() == replay::PriceLevel{max_price, max_quantity}, "large valid bid level is stored");

  replay::OrderBook overflow_mid;
  overflow_mid.apply(update(replay::Side::Buy, max_price, 1));
  overflow_mid.apply(update(replay::Side::Sell, max_price, 1));
  expect_throws<std::overflow_error>([&overflow_mid] { static_cast<void>(overflow_mid.mid_price_x2_ticks()); },
                                     "mid x2 overflow is explicit");

  expect_throws<std::invalid_argument>(
      [] {
        replay::OrderBook invalid;
        invalid.apply(update(replay::Side::Buy, -1, 1));
      },
      "negative price ticks are rejected");
  expect_throws<std::invalid_argument>(
      [] {
        replay::OrderBook invalid;
        invalid.apply(update(replay::Side::Buy, 10000, -1));
      },
      "negative quantity is rejected");
}

void test_golden_replay_and_determinism() {
  const replay::FeedParserConfig config{.tick_size = "0.01", .price_format = replay::PriceFieldFormat::Ticks};
  const auto feed = replay::load_book_updates_csv("tests/golden/order_book_updates.csv", config);
  expect_true(feed.size() == 12, "golden fixture event count");

  std::optional<std::string> expected_state;
  std::optional<std::uint64_t> expected_hash;

  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::OrderBook book;
    for (const auto& event : feed.events()) {
      book.apply(event.book_update());
      verify_invariants(book);
    }

    expect_true(book.best_bid() == replay::PriceLevel{10005, 1}, "golden final best bid");
    expect_true(book.best_ask() == replay::PriceLevel{10002, 8}, "golden final best ask");
    expect_true(book.spread_ticks() == -3, "golden final spread");
    expect_true(book.mid_price_x2_ticks() == 20007, "golden final mid x2");
    expect_true(book.bid_level_count() == 3, "golden final bid count");
    expect_true(book.ask_level_count() == 2, "golden final ask count");
    expect_true(book.is_crossed(), "golden final crossed state is preserved");
    expect_true(contains_level(book.top_bids(10), replay::PriceLevel{10000, 7}), "golden retained updated bid");
    expect_true(!book.bid_quantity_at(9999).has_value(), "golden deleted bid absent");
    expect_true(!book.ask_quantity_at(10001).has_value(), "golden deleted ask absent");

    const auto state = book.canonical_state();
    const auto hash = book.state_hash();
    expect_true(state == "B,10005,1\nB,10000,7\nB,9998,9\nA,10002,8\nA,10003,6\n",
                "golden final canonical state");
    expect_true(hash == 0xAA697F6F231EBC7DULL, "golden final FNV-1a hash");

    if (!expected_state.has_value()) {
      expected_state = state;
      expected_hash = hash;
    } else {
      expect_true(state == expected_state, "golden canonical state repeatability");
      expect_true(hash == expected_hash, "golden hash repeatability");
    }
  }
}

}  // namespace

int main() {
  test_basic_insert();
  test_price_priority();
  test_update_replace();
  test_delete();
  test_delete_nonexistent_level();
  test_empty_book();
  test_one_sided_book();
  test_spread_and_mid();
  test_top_n();
  test_locked_and_crossed_books();
  test_extreme_values_and_validation();
  test_golden_replay_and_determinism();

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
