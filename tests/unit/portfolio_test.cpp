#include "replay/execution_simulator.hpp"
#include "replay/latency_execution.hpp"
#include "replay/order.hpp"
#include "replay/portfolio.hpp"

#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
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

replay::Fill fill(replay::FillSequenceId sequence_id,
                  replay::Side side,
                  replay::PriceTicks price_ticks,
                  replay::Quantity quantity,
                  replay::FeeAmount fee_amount = 0,
                  replay::OrderId order_id = 1,
                  replay::TimestampNs timestamp_ns = 100) {
  return replay::Fill{.order_id = order_id,
                      .side = side,
                      .price_ticks = price_ticks,
                      .quantity = quantity,
                      .fill_timestamp_ns = timestamp_ns,
                      .fill_sequence_id = sequence_id,
                      .fee_amount = fee_amount};
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

std::vector<std::string> lot_strings(const replay::Portfolio& portfolio) {
  std::vector<std::string> output;
  output.reserve(portfolio.open_lots().size());
  for (const auto& lot : portfolio.open_lots()) {
    output.push_back(replay::canonical_lot_string(lot));
  }
  return output;
}

void expect_identity(const replay::Portfolio& portfolio,
                     const replay::OrderBook& book,
                     std::string_view message) {
  const auto mark = portfolio.mark_to_market(book);
  expect_true(mark.has_value(), std::string{message} + ": mark available");
  if (!mark.has_value()) {
    return;
  }

  const auto equity_change_x2 = replay::checked_accounting_sub(
      mark->equity_x2, replay::checked_accounting_x2(portfolio.initial_cash(), "initial_cash_x2"), "equity_change_x2");
  expect_true(equity_change_x2 == mark->net_total_pnl_x2, std::string{message} + ": equity and net PnL identity");
}

void test_initial_state_and_cash_only_mark() {
  const replay::Portfolio portfolio{1000};
  const auto mark = portfolio.cash_only_mark();

  expect_true(portfolio.initial_cash() == 1000, "initial cash stored");
  expect_true(portfolio.cash() == 1000, "initial cash is current cash");
  expect_true(portfolio.inventory() == 0, "initial inventory zero");
  expect_true(portfolio.realized_gross_pnl() == 0, "initial realized gross zero");
  expect_true(portfolio.total_fees() == 0, "initial fees zero");
  expect_true(portfolio.turnover() == 0, "initial turnover zero");
  expect_true(portfolio.fill_count() == 0, "initial fill count zero");
  expect_true(portfolio.open_lots().empty(), "initial lots empty");
  expect_true(portfolio.ledger().empty(), "initial ledger empty");
  expect_true(!mark.mid_price_x2_ticks.has_value(), "cash-only mark has no midpoint");
  expect_true(mark.equity_x2 == 2000, "cash-only equity x2");
  expect_true(mark.net_total_pnl_x2 == 0, "cash-only net PnL unchanged from initial cash");
}

void test_single_open_buy_and_sell() {
  replay::Portfolio long_portfolio;
  long_portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 10));
  expect_true(long_portfolio.cash() == -1000, "buy decreases cash by notional");
  expect_true(long_portfolio.inventory() == 10, "buy creates positive inventory");
  expect_true((lot_strings(long_portfolio) == std::vector<std::string>{"long,10,100"}), "buy opens long lot");
  expect_true(long_portfolio.turnover() == 1000, "buy adds absolute notional turnover");

  replay::Portfolio short_portfolio;
  short_portfolio.apply_fill(fill(1, replay::Side::Sell, 100, 10));
  expect_true(short_portfolio.cash() == 1000, "sell increases cash by notional");
  expect_true(short_portfolio.inventory() == -10, "sell creates negative inventory");
  expect_true((lot_strings(short_portfolio) == std::vector<std::string>{"short,10,100"}), "sell opens short lot");
}

void test_round_trip_fees_and_ledger() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 10, 100, 2, 10, 1000));
  portfolio.apply_fill(fill(2, replay::Side::Sell, 11, 100, 3, 10, 1001));

  expect_true(portfolio.inventory() == 0, "round trip flat");
  expect_true(portfolio.realized_gross_pnl() == 100, "long round-trip realized gross before fees");
  expect_true(portfolio.total_fees() == 5, "total fees accumulated");
  expect_true(portfolio.cash() == 95, "cash reflects realized minus fees from zero initial cash");
  expect_true(portfolio.turnover() == 2100, "turnover uses absolute traded notional");
  expect_true(portfolio.fill_count() == 2, "fill count increments");
  expect_true(portfolio.ledger().size() == 2, "ledger records fills");
  expect_true(portfolio.ledger().back().cash_after == 95, "ledger cash after second fill");
  expect_true(portfolio.ledger().back().inventory_after == 0, "ledger inventory after second fill");
  expect_true(portfolio.ledger().back().realized_gross_pnl_after == 100, "ledger realized after second fill");

  const auto mark = portfolio.cash_only_mark();
  expect_true(mark.net_total_pnl_x2 == 190, "net total PnL subtracts total fees");
}

void test_simple_long_round_trip_no_fees() {
  replay::Portfolio portfolio{500};
  portfolio.apply_fill(fill(1, replay::Side::Buy, 10, 100));
  portfolio.apply_fill(fill(2, replay::Side::Sell, 11, 100));

  expect_true(portfolio.inventory() == 0, "simple long round trip flat");
  expect_true(portfolio.realized_gross_pnl() == 100, "simple long round trip realized gross");
  expect_true(portfolio.cash() - portfolio.initial_cash() == 100, "simple long round trip cash gain");
  expect_true(portfolio.open_lots().empty(), "simple long round trip leaves no lots");
}

void test_partial_long_close_fifo() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 10, 100));
  portfolio.apply_fill(fill(2, replay::Side::Sell, 11, 40));

  expect_true(portfolio.inventory() == 60, "partial long close leaves long inventory");
  expect_true(portfolio.realized_gross_pnl() == 40, "partial long close realized gross");
  expect_true((lot_strings(portfolio) == std::vector<std::string>{"long,60,10"}), "partial long remaining lot");
}

void test_short_round_trip_and_partial_cover() {
  replay::Portfolio round_trip;
  round_trip.apply_fill(fill(1, replay::Side::Sell, 11, 100));
  round_trip.apply_fill(fill(2, replay::Side::Buy, 10, 100));
  expect_true(round_trip.inventory() == 0, "short round trip flat");
  expect_true(round_trip.realized_gross_pnl() == 100, "short round-trip realized gross before fees");

  replay::Portfolio partial;
  partial.apply_fill(fill(1, replay::Side::Sell, 11, 100));
  partial.apply_fill(fill(2, replay::Side::Buy, 10, 40));
  expect_true(partial.inventory() == -60, "partial short cover leaves negative inventory");
  expect_true(partial.realized_gross_pnl() == 40, "partial short cover realized gross");
  expect_true((lot_strings(partial) == std::vector<std::string>{"short,60,11"}), "partial short remaining lot");
}

void test_fifo_long_and_short_multi_lot() {
  replay::Portfolio long_fifo;
  long_fifo.apply_fill(fill(1, replay::Side::Buy, 100, 5));
  long_fifo.apply_fill(fill(2, replay::Side::Buy, 110, 5));
  long_fifo.apply_fill(fill(3, replay::Side::Sell, 120, 7));
  expect_true(long_fifo.realized_gross_pnl() == 120, "long FIFO realized first lot before second");
  expect_true((lot_strings(long_fifo) == std::vector<std::string>{"long,3,110"}), "long FIFO leaves second lot remainder");

  replay::Portfolio short_fifo;
  short_fifo.apply_fill(fill(1, replay::Side::Sell, 120, 5));
  short_fifo.apply_fill(fill(2, replay::Side::Sell, 110, 5));
  short_fifo.apply_fill(fill(3, replay::Side::Buy, 100, 7));
  expect_true(short_fifo.realized_gross_pnl() == 120, "short FIFO realized first lot before second");
  expect_true((lot_strings(short_fifo) == std::vector<std::string>{"short,3,110"}), "short FIFO leaves second lot remainder");
}

void test_long_to_short_and_short_to_long_flips() {
  replay::Portfolio long_to_short;
  long_to_short.apply_fill(fill(1, replay::Side::Buy, 100, 5));
  long_to_short.apply_fill(fill(2, replay::Side::Sell, 110, 8));
  expect_true(long_to_short.inventory() == -3, "oversell flips long to short");
  expect_true(long_to_short.realized_gross_pnl() == 50, "long-to-short flip realized closed long");
  expect_true((lot_strings(long_to_short) == std::vector<std::string>{"short,3,110"}), "long-to-short opens short remainder");

  replay::Portfolio short_to_long;
  short_to_long.apply_fill(fill(1, replay::Side::Sell, 110, 5));
  short_to_long.apply_fill(fill(2, replay::Side::Buy, 100, 8));
  expect_true(short_to_long.inventory() == 3, "overbuy flips short to long");
  expect_true(short_to_long.realized_gross_pnl() == 50, "short-to-long flip realized closed short");
  expect_true((lot_strings(short_to_long) == std::vector<std::string>{"long,3,100"}), "short-to-long opens long remainder");
}

void test_multiple_fills_with_same_order_id() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 3, 0, 99));
  portfolio.apply_fill(fill(2, replay::Side::Buy, 100, 2, 0, 99));

  expect_true(portfolio.inventory() == 5, "multiple fills from one order accumulate inventory");
  expect_true(portfolio.cash() == -500, "multiple fills from one order update cash");
  expect_true((lot_strings(portfolio) == std::vector<std::string>{"long,3,100", "long,2,100"}),
              "multiple same-order fills remain distinct FIFO lots");
}

void test_aggressive_and_passive_economic_fill_equivalence() {
  replay::Portfolio aggressive;
  replay::Portfolio passive;

  aggressive.apply_fill(fill(1, replay::Side::Buy, 10000, 4, 2, 10, 200));
  passive.apply_fill(fill(1, replay::Side::Buy, 10000, 4, 2, 20, 300));

  expect_true(aggressive.cash() == passive.cash(), "same economic aggressive/passive fill cash equivalence");
  expect_true(aggressive.inventory() == passive.inventory(), "same economic aggressive/passive fill inventory equivalence");
  expect_true(aggressive.total_fees() == passive.total_fees(), "same economic aggressive/passive fill fee equivalence");
  expect_true(aggressive.turnover() == passive.turnover(), "same economic aggressive/passive fill turnover equivalence");
  expect_true(lot_strings(aggressive) == lot_strings(passive), "same economic aggressive/passive fill lot equivalence");
}

void test_orders_do_not_mutate_portfolio_without_fills() {
  replay::Portfolio portfolio;
  replay::ExecutionSimulator simulator;
  auto order = simulator.create_order(replay::Side::Buy, replay::OrderType::Limit, 5, 100, 99);
  order.transition_to(replay::OrderStatus::Pending);
  order.transition_to(replay::OrderStatus::Acknowledged);
  order.transition_to(replay::OrderStatus::Canceled);

  expect_true(portfolio.cash() == 0, "order lifecycle alone does not mutate cash");
  expect_true(portfolio.inventory() == 0, "order lifecycle alone does not mutate inventory");
  expect_true(portfolio.fill_count() == 0, "order lifecycle alone does not add fills");
}

void test_cancel_does_not_reverse_prior_fills() {
  auto book = book_from({update(replay::Side::Buy, 10000, 0), update(replay::Side::Sell, 10005, 1)});
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  replay::LatencyAwareExecution execution;
  const auto& submission =
      execution.submit_order_intent(replay::OrderIntent{.side = replay::Side::Buy,
                                                        .quantity = 10,
                                                        .order_type = replay::OrderType::Limit,
                                                        .limit_price_ticks = 10000,
                                                        .decision_timestamp_ns = 100},
                                    replay::LatencyNs{},
                                    loop);
  static_cast<void>(execution.process_order_arrival(submission.arrival_event, book));
  execution.process_trade(replay::TradeEvent{replay::EventKey{200, 1}, 10000, 3, replay::Side::Sell});

  replay::Portfolio portfolio;
  for (const auto& generated_fill : execution.fills()) {
    portfolio.apply_fill(generated_fill);
  }
  const auto cash_after_fill = portfolio.cash();
  const auto inventory_after_fill = portfolio.inventory();
  const auto turnover_after_fill = portfolio.turnover();

  const auto cancel = execution.request_cancel(1, replay::LatencyNs{}, loop);
  execution.process_cancel_arrival(cancel);

  expect_true(portfolio.cash() == cash_after_fill, "cancel does not reverse prior fill cash");
  expect_true(portfolio.inventory() == inventory_after_fill, "cancel does not reverse prior fill inventory");
  expect_true(portfolio.turnover() == turnover_after_fill, "cancel does not reverse prior fill turnover");
  expect_true(execution.final_orders().front().status() == replay::OrderStatus::Canceled, "execution order canceled");
}

void test_turnover_exact_absolute_notional() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 2));
  portfolio.apply_fill(fill(2, replay::Side::Sell, 110, 3));

  expect_true(portfolio.turnover() == 530, "turnover is cumulative absolute traded notional");
}

void test_half_tick_mark_and_pnl_identity() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 10000, 1));
  const auto book = book_from({update(replay::Side::Buy, 10000, 5), update(replay::Side::Sell, 10001, 5)});
  const auto mark = portfolio.mark_to_market(book);

  expect_true(mark.has_value(), "half-tick mark available");
  expect_true(mark->mid_price_x2_ticks == 20001, "half-tick midpoint represented as doubled ticks");
  expect_true(mark->unrealized_gross_pnl_x2 == 1, "half-tick unrealized PnL exact");
  expect_true(mark->equity_x2 == 1, "equity uses doubled cash plus inventory times doubled mid");
  expect_true(mark->net_total_pnl_x2 == 1, "net total PnL x2 matches half-tick mark");
  expect_identity(portfolio, book, "half-tick long");
}

void test_equity_identity_long_short_partial_and_flip() {
  const auto book = book_from({update(replay::Side::Buy, 109, 10), update(replay::Side::Sell, 110, 10)});

  replay::Portfolio partial_long{1000};
  partial_long.apply_fill(fill(1, replay::Side::Buy, 100, 2));
  partial_long.apply_fill(fill(2, replay::Side::Sell, 110, 1, 2));
  expect_identity(partial_long, book, "partial long with fees");

  replay::Portfolio short_position{1000};
  short_position.apply_fill(fill(1, replay::Side::Sell, 120, 3, 1));
  short_position.apply_fill(fill(2, replay::Side::Buy, 110, 1, 1));
  expect_identity(short_position, book, "partial short with fees");

  replay::Portfolio flipped{1000};
  flipped.apply_fill(fill(1, replay::Side::Buy, 100, 2));
  flipped.apply_fill(fill(2, replay::Side::Sell, 111, 5, 1));
  expect_identity(flipped, book, "flipped short with fees");
}

void test_mark_availability_policies() {
  replay::Portfolio nonzero;
  nonzero.apply_fill(fill(1, replay::Side::Buy, 100, 1));

  replay::OrderBook empty;
  const auto bids_only = book_from({update(replay::Side::Buy, 100, 1)});
  const auto asks_only = book_from({update(replay::Side::Sell, 101, 1)});
  const auto locked = book_from({update(replay::Side::Buy, 100, 1), update(replay::Side::Sell, 100, 1)});
  const auto crossed = book_from({update(replay::Side::Buy, 102, 1), update(replay::Side::Sell, 101, 1)});

  expect_true(!nonzero.mark_to_market(empty).has_value(), "nonzero inventory cannot mark empty book");
  expect_true(!nonzero.mark_to_market(bids_only).has_value(), "nonzero inventory cannot mark bid-only book");
  expect_true(!nonzero.mark_to_market(asks_only).has_value(), "nonzero inventory cannot mark ask-only book");
  expect_true(nonzero.mark_to_market(locked).has_value(), "locked two-sided book can mark");
  expect_true(!nonzero.mark_to_market(crossed).has_value(), "crossed book mark unavailable");

  replay::Portfolio flat{7};
  const auto flat_empty_mark = flat.mark_to_market(empty);
  const auto flat_crossed_mark = flat.mark_to_market(crossed);
  expect_true(flat_empty_mark.has_value() && flat_empty_mark->equity_x2 == 14, "flat book can mark cash on empty book");
  expect_true(flat_crossed_mark.has_value() && flat_crossed_mark->equity_x2 == 14, "flat book can mark cash on crossed book");
}

void test_invalid_fills_fail_explicitly() {
  replay::Portfolio portfolio;
  expect_throws<std::invalid_argument>([&portfolio] { portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 0)); },
                                       "zero quantity fill rejected");
  expect_throws<std::invalid_argument>([&portfolio] { portfolio.apply_fill(fill(2, replay::Side::Buy, -1, 1)); },
                                       "negative price fill rejected");
  expect_throws<std::invalid_argument>([&portfolio] { portfolio.apply_fill(fill(3, replay::Side::Buy, 100, -1)); },
                                       "negative quantity fill rejected");
  expect_throws<std::invalid_argument>([&portfolio] { portfolio.apply_fill(fill(4, replay::Side::Buy, 100, 1, -1)); },
                                       "negative fee rejected");
  expect_throws<std::invalid_argument>(
      [&portfolio] { portfolio.apply_fill(fill(5, static_cast<replay::Side>(42), 100, 1)); },
      "invalid side rejected");

  expect_true(portfolio.fill_count() == 0, "invalid fills do not change fill count");
  expect_true(portfolio.cash() == 0 && portfolio.inventory() == 0, "invalid fills do not mutate state");
}

void test_duplicate_fill_sequence_rejected() {
  replay::Portfolio portfolio;
  portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 1));
  expect_throws<std::invalid_argument>([&portfolio] { portfolio.apply_fill(fill(1, replay::Side::Sell, 101, 1)); },
                                       "duplicate fill sequence rejected");
  expect_true(portfolio.cash() == -100, "duplicate fill does not mutate cash");
  expect_true(portfolio.inventory() == 1, "duplicate fill does not mutate inventory");
  expect_true(portfolio.fill_count() == 1, "duplicate fill does not mutate fill count");
}

void test_determinism_over_repeated_runs() {
  std::vector<std::string> expected_lots;
  replay::AccountingAmount expected_cash = 0;
  replay::AccountingAmount expected_realized = 0;
  replay::FeeAmount expected_fees = 0;
  replay::AccountingAmount expected_turnover = 0;
  replay::AccountingAmountX2 expected_equity_x2 = 0;
  replay::AccountingAmountX2 expected_net_pnl_x2 = 0;
  const auto book = book_from({update(replay::Side::Buy, 105, 10), update(replay::Side::Sell, 106, 10)});

  for (int iteration = 0; iteration < 100; ++iteration) {
    replay::Portfolio portfolio{1000};
    portfolio.apply_fill(fill(1, replay::Side::Buy, 100, 5, 1));
    portfolio.apply_fill(fill(2, replay::Side::Buy, 102, 2, 1));
    portfolio.apply_fill(fill(3, replay::Side::Sell, 106, 6, 2));
    portfolio.apply_fill(fill(4, replay::Side::Sell, 104, 3, 1));
    const auto mark = portfolio.mark_to_market(book);

    if (iteration == 0) {
      expected_lots = lot_strings(portfolio);
      expected_cash = portfolio.cash();
      expected_realized = portfolio.realized_gross_pnl();
      expected_fees = portfolio.total_fees();
      expected_turnover = portfolio.turnover();
      expected_equity_x2 = mark->equity_x2;
      expected_net_pnl_x2 = mark->net_total_pnl_x2;
    }

    expect_true(lot_strings(portfolio) == expected_lots, "deterministic lots over 100 runs");
    expect_true(portfolio.cash() == expected_cash, "deterministic cash over 100 runs");
    expect_true(portfolio.realized_gross_pnl() == expected_realized, "deterministic realized PnL over 100 runs");
    expect_true(portfolio.total_fees() == expected_fees, "deterministic fees over 100 runs");
    expect_true(portfolio.turnover() == expected_turnover, "deterministic turnover over 100 runs");
    expect_true(mark.has_value() && mark->equity_x2 == expected_equity_x2, "deterministic equity mark over 100 runs");
    expect_true(mark.has_value() && mark->net_total_pnl_x2 == expected_net_pnl_x2,
                "deterministic net PnL mark over 100 runs");
  }
}

void test_overflow_checks_and_state_preservation() {
  constexpr auto max_value = std::numeric_limits<replay::AccountingAmount>::max();
  expect_throws<std::overflow_error>(
      [] {
        static_cast<void>(replay::checked_notional_amount(std::numeric_limits<replay::PriceTicks>::max(),
                                                          std::numeric_limits<replay::Quantity>::max()));
      },
      "notional overflow rejected");
  expect_throws<std::overflow_error>(
      [] { static_cast<void>(replay::checked_accounting_add(max_value, 1, "test")); },
      "accounting addition overflow rejected");
  expect_throws<std::overflow_error>(
      [] { static_cast<void>(replay::checked_accounting_x2(max_value, "test")); },
      "doubled accounting overflow rejected");

  replay::Portfolio portfolio{max_value};
  expect_throws<std::overflow_error>([&portfolio] { portfolio.apply_fill(fill(1, replay::Side::Sell, 1, 1)); },
                                     "cash overflow during fill rejected");
  expect_true(portfolio.cash() == max_value, "overflowing fill does not mutate cash");
  expect_true(portfolio.inventory() == 0, "overflowing fill does not mutate inventory");
  expect_true(portfolio.fill_count() == 0, "overflowing fill does not mutate fill count");
}

void test_execution_fills_drive_accounting() {
  const auto book = book_from({update(replay::Side::Sell, 10001, 2), update(replay::Side::Sell, 10002, 3)});
  replay::ExecutionSimulator simulator{replay::ExecutionConfig{.fee_model = replay::FeeModel{100}}};
  const auto result =
      simulator.execute_order(simulator.create_order(replay::Side::Buy, replay::OrderType::Market, 3, 500), book);

  replay::Portfolio portfolio;
  for (const auto& generated_fill : result.fills) {
    portfolio.apply_fill(generated_fill);
  }

  expect_true(result.fills.size() == 2, "execution integration generated two fills");
  expect_true(portfolio.inventory() == 3, "execution fills create inventory");
  expect_true(portfolio.cash() == -30007, "execution fills drive cash and fees");
  expect_true(portfolio.total_fees() == 3, "execution fill fees accumulated");
  expect_true(portfolio.turnover() == 30004, "execution fill turnover accumulated");
}

void test_passive_limit_fills_drive_accounting_only_when_generated() {
  auto book = book_from({update(replay::Side::Buy, 10000, 0), update(replay::Side::Sell, 10005, 1)});
  replay::EventLoop loop{std::vector<replay::MarketEvent>{}};
  replay::LatencyAwareExecution execution;
  const auto& submission =
      execution.submit_order_intent(replay::OrderIntent{.side = replay::Side::Buy,
                                                        .quantity = 4,
                                                        .order_type = replay::OrderType::Limit,
                                                        .limit_price_ticks = 10000,
                                                        .decision_timestamp_ns = 100},
                                    replay::LatencyNs{},
                                    loop);

  replay::Portfolio portfolio;
  static_cast<void>(execution.process_order_arrival(submission.arrival_event, book));
  expect_true(portfolio.inventory() == 0, "limit order arrival without fills does not mutate accounting");

  execution.process_trade(replay::TradeEvent{replay::EventKey{200, 1}, 10000, 4, replay::Side::Sell});
  for (const auto& generated_fill : execution.fills()) {
    portfolio.apply_fill(generated_fill);
  }
  expect_true(portfolio.inventory() == 4, "passive limit fill records drive accounting");
  expect_true(portfolio.cash() == -40000, "passive limit fill cash");
}

}  // namespace

int main() {
  test_initial_state_and_cash_only_mark();
  test_single_open_buy_and_sell();
  test_round_trip_fees_and_ledger();
  test_simple_long_round_trip_no_fees();
  test_partial_long_close_fifo();
  test_short_round_trip_and_partial_cover();
  test_fifo_long_and_short_multi_lot();
  test_long_to_short_and_short_to_long_flips();
  test_multiple_fills_with_same_order_id();
  test_aggressive_and_passive_economic_fill_equivalence();
  test_orders_do_not_mutate_portfolio_without_fills();
  test_cancel_does_not_reverse_prior_fills();
  test_turnover_exact_absolute_notional();
  test_half_tick_mark_and_pnl_identity();
  test_equity_identity_long_short_partial_and_flip();
  test_mark_availability_policies();
  test_invalid_fills_fail_explicitly();
  test_duplicate_fill_sequence_rejected();
  test_determinism_over_repeated_runs();
  test_overflow_checks_and_state_preservation();
  test_execution_fills_drive_accounting();
  test_passive_limit_fills_drive_accounting_only_when_generated();

  if (failures != 0) {
    std::cerr << failures << " portfolio test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
