#include "replay/replay_engine.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
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

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    throw std::runtime_error{"failed to open test file: " + path.string()};
  }
  std::ostringstream content;
  content << input.rdbuf();
  return content.str();
}

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output{path, std::ios::binary};
  output << content;
}

void remove_path(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
}

replay::ReplayRunResult run_example(const std::filesystem::path& output) {
  std::filesystem::create_directories(output.parent_path());
  return replay::run_replay_from_config_file("configs/example_config.kv", output, true);
}

void expect_golden_artifacts(const replay::ReplayRunResult& result) {
  expect_true(result.artifacts.orders_csv == read_file("tests/golden/expected_orders.csv"), "orders golden artifact");
  expect_true(result.artifacts.fills_csv == read_file("tests/golden/expected_fills.csv"), "fills golden artifact");
  expect_true(result.artifacts.portfolio_summary_json == read_file("tests/golden/expected_portfolio_summary.json"),
              "portfolio summary golden artifact");
  expect_true(result.artifacts.metrics_json == read_file("tests/golden/expected_metrics.json"), "metrics golden artifact");
  expect_true(result.artifacts.run_manifest_json == read_file("tests/golden/expected_run_manifest.json"),
              "run manifest golden artifact");
  expect_true(result.artifacts.ledger_csv == read_file("tests/golden/expected_ledger.csv"), "ledger golden artifact");
}

void expect_order_conservation(const replay::ReplayRunResult& result) {
  for (const auto& order : result.final_orders) {
    replay::Quantity filled_by_records = 0;
    for (const auto& fill : result.fills) {
      if (fill.order_id == order.order_id()) {
        filled_by_records += fill.quantity;
      }
    }
    expect_true(filled_by_records == order.filled_quantity(), "order filled quantity equals sum of fills");
    expect_true(order.filled_quantity() + order.remaining_quantity() == order.original_quantity(),
                "filled plus remaining equals original");
  }
}

void test_valid_end_to_end_run_and_golden_outputs() {
  const auto output = std::filesystem::path{"build/test_e2e_run"};
  remove_path(output);
  const auto result = run_example(output);

  expect_true(std::filesystem::exists(output / "run_manifest.json"), "manifest artifact exists");
  expect_true(std::filesystem::exists(output / "orders.csv"), "orders artifact exists");
  expect_true(std::filesystem::exists(output / "fills.csv"), "fills artifact exists");
  expect_true(std::filesystem::exists(output / "portfolio_summary.json"), "portfolio summary artifact exists");
  expect_true(std::filesystem::exists(output / "metrics.json"), "metrics artifact exists");
  expect_true(std::filesystem::exists(output / "ledger.csv"), "ledger artifact exists");

  expect_true(result.metrics.book_updates_processed == 3, "exact book update count");
  expect_true(result.metrics.trades_processed == 2, "exact trade count");
  expect_true(result.metrics.market_events_processed == 5, "exact historical market event count");
  expect_true(result.metrics.internal_events_processed == 3, "exact internal event count");
  expect_true(result.metrics.strategy_intents == 2, "exact strategy intent count");
  expect_true(result.metrics.orders_submitted == 2, "exact submitted order count");
  expect_true(result.metrics.orders_canceled == 2, "exact canceled order count");
  expect_true(result.metrics.fills == 3, "exact fill count");

  expect_true(result.portfolio.cash() == -50009, "exact ending cash");
  expect_true(result.portfolio.inventory() == 5, "exact ending inventory");
  expect_true(result.portfolio.realized_gross_pnl() == 0, "exact realized gross pnl");
  expect_true(result.portfolio.total_fees() == 5, "exact total fees");
  expect_true(result.portfolio.turnover() == 50004, "exact turnover");
  expect_true(result.final_mark.has_value(), "final mark available");
  expect_true(result.final_mark->mid_price_x2_ticks == 20002, "exact final mid x2");
  expect_true(result.final_mark->unrealized_gross_pnl_x2 == 2, "exact unrealized gross pnl x2");
  expect_true(result.final_mark->equity_x2 == -8, "exact ending equity x2");
  expect_true(result.final_mark->net_total_pnl_x2 == -8, "exact net total pnl x2");
  expect_true(result.hashes.final_book_hash == "9ca1786003897355", "exact final book hash");
  expect_true(result.hashes.run_hash == "8aca37583ca6f83a", "exact run hash");
  expect_golden_artifacts(result);
  expect_order_conservation(result);
}

void test_repeatability_and_output_directory_independence() {
  std::optional<replay::ReplayHashes> expected_hashes;
  for (int iteration = 0; iteration < 100; ++iteration) {
    const auto output = std::filesystem::path{"build/test_repeat_" + std::to_string(iteration)};
    remove_path(output);
    const auto result = run_example(output);
    if (!expected_hashes.has_value()) {
      expected_hashes = result.hashes;
    }
    expect_true(result.hashes.input_hash == expected_hashes->input_hash, "repeatable input hash");
    expect_true(result.hashes.config_hash == expected_hashes->config_hash, "repeatable config hash");
    expect_true(result.hashes.orders_hash == expected_hashes->orders_hash, "repeatable orders hash");
    expect_true(result.hashes.fills_hash == expected_hashes->fills_hash, "repeatable fills hash");
    expect_true(result.hashes.portfolio_hash == expected_hashes->portfolio_hash, "repeatable portfolio hash");
    expect_true(result.hashes.final_book_hash == expected_hashes->final_book_hash, "repeatable final book hash");
    expect_true(result.hashes.run_hash == expected_hashes->run_hash, "repeatable run hash");
  }

  const auto first = run_example("build/test_output_a");
  const auto second = run_example("build/test_output_b");
  expect_true(first.hashes.run_hash == second.hashes.run_hash, "different output directory preserves run hash");
}

void test_same_content_different_source_path() {
  const auto temp_dir = std::filesystem::path{"build/copied_inputs"};
  remove_path(temp_dir);
  std::filesystem::create_directories(temp_dir);
  std::filesystem::copy_file("tests/golden/e2e_book_updates.csv", temp_dir / "book.csv");
  std::filesystem::copy_file("tests/golden/e2e_trades.csv", temp_dir / "trades.csv");
  write_file(temp_dir / "config.kv",
             "book_updates_path=build/copied_inputs/book.csv\n"
             "trades_path=build/copied_inputs/trades.csv\n"
             "output_directory=build/copied_inputs/out\n"
             "tick_size=0.01\n"
             "price_format=ticks\n"
             "strategy_type=scripted\n"
             "scripted_intents=100:2:buy:market:3:;200:3:buy:limit:6:10000\n"
             "order_latency_ns=50\n"
             "cancel_after_arrival_ns=100\n"
             "queue_fraction_ppm=500000\n"
             "fee_rate_ppm=100\n"
             "initial_cash=0\n");

  const auto original = run_example("build/original_source_path");
  const auto copied = replay::run_replay_from_config_file(temp_dir / "config.kv", std::nullopt, true);
  expect_true(original.hashes.book_updates_hash == copied.hashes.book_updates_hash, "same book content hash");
  expect_true(original.hashes.trades_hash == copied.hashes.trades_hash, "same trade content hash");
  expect_true(original.hashes.input_hash == copied.hashes.input_hash, "same combined input hash");
  expect_true(original.hashes.run_hash == copied.hashes.run_hash, "same run hash for copied input content");
}

void test_config_changes() {
  auto base = replay::load_replay_config("configs/example_config.kv");
  auto latency_changed = base;
  latency_changed.order_latency = replay::LatencyNs::from_nanoseconds(0);
  auto queue_changed = base;
  queue_changed.queue_fraction_ppm = 1'000'000;

  const replay::ReplayEngine base_engine{base};
  const replay::ReplayEngine latency_engine{latency_changed};
  const replay::ReplayEngine queue_engine{queue_changed};
  const auto base_result = base_engine.run();
  const auto latency_result = latency_engine.run();
  const auto queue_result = queue_engine.run();

  expect_true(base_result.hashes.input_hash == latency_result.hashes.input_hash, "latency change keeps input hash");
  expect_true(base_result.hashes.config_hash != latency_result.hashes.config_hash, "latency change changes config hash");
  expect_true(base_result.hashes.run_hash != latency_result.hashes.run_hash, "latency change changes run hash");
  expect_true(base_result.hashes.config_hash != queue_result.hashes.config_hash, "queue fraction change changes config hash");
  expect_true(base_result.hashes.run_hash != queue_result.hashes.run_hash, "queue fraction change changes run hash");
}

void test_error_paths_and_output_policy() {
  expect_throws<std::invalid_argument>(
      [] { static_cast<void>(replay::load_replay_config("tests/golden/e2e_invalid_config.kv")); },
      "invalid config rejected");
  expect_throws<std::runtime_error>(
      [] {
        auto config = replay::load_replay_config("configs/example_config.kv");
        config.book_updates_path = "tests/golden/missing.csv";
        const replay::ReplayEngine engine{config};
        static_cast<void>(engine.run());
      },
      "missing input rejected");
  expect_throws<replay::ParseError>(
      [] {
        auto config = replay::load_replay_config("configs/example_config.kv");
        config.book_updates_path = "tests/golden/e2e_malformed_book_updates.csv";
        const replay::ReplayEngine engine{config};
        static_cast<void>(engine.run());
      },
      "malformed input rejected");

  const auto output = std::filesystem::path{"build/test_output_policy"};
  remove_path(output);
  static_cast<void>(run_example(output));
  expect_throws<std::runtime_error>(
      [&output] { static_cast<void>(replay::run_replay_from_config_file("configs/example_config.kv", output, false)); },
      "existing output without force rejected");
  static_cast<void>(replay::run_replay_from_config_file("configs/example_config.kv", output, true));

  const auto invalid_output = std::filesystem::path{"build/output_as_file"};
  remove_path(invalid_output);
  write_file(invalid_output, "not a directory\n");
  expect_throws<std::runtime_error>(
      [&invalid_output] {
        static_cast<void>(replay::run_replay_from_config_file("configs/example_config.kv", invalid_output, true));
      },
      "file output path rejected");
}

void test_empty_feed_and_mark_unavailable_cases() {
  write_file("build/empty_config.kv",
             "book_updates_path=tests/golden/e2e_empty_book_updates.csv\n"
             "trades_path=tests/golden/e2e_empty_trades.csv\n"
             "output_directory=build/empty_run\n"
             "price_format=ticks\n"
             "strategy_type=queue_imbalance\n"
             "order_latency_ns=0\n"
             "queue_fraction_ppm=0\n"
             "fee_rate_ppm=0\n"
             "initial_cash=123\n");
  const auto empty = replay::run_replay_from_config_file("build/empty_config.kv", std::nullopt, true);
  expect_true(empty.metrics.market_events_processed == 0, "empty feed event count");
  expect_true(empty.metrics.orders_submitted == 0, "empty feed order count");
  expect_true(empty.portfolio.cash() == 123, "empty feed preserves cash");
  expect_true(empty.final_mark.has_value(), "flat empty feed has cash-only mark");
  expect_true(!empty.final_mark->mid_price_x2_ticks.has_value(), "empty feed has no midpoint");
  expect_true(empty.final_mark->equity_x2 == 246, "empty feed cash-only equity");

  write_file("build/one_sided_config.kv",
             "book_updates_path=tests/golden/e2e_one_sided_book_updates.csv\n"
             "trades_path=tests/golden/e2e_one_sided_trades.csv\n"
             "output_directory=build/one_sided_run\n"
             "price_format=ticks\n"
             "strategy_type=scripted\n"
             "scripted_intents=100:1:buy:market:1:\n"
             "order_latency_ns=0\n"
             "queue_fraction_ppm=0\n"
             "fee_rate_ppm=0\n"
             "initial_cash=0\n");
  const auto one_sided = replay::run_replay_from_config_file("build/one_sided_config.kv", std::nullopt, true);
  expect_true(one_sided.portfolio.inventory() == 1, "one-sided final run has open inventory");
  expect_true(!one_sided.final_mark.has_value(), "nonzero inventory one-sided final mark unavailable");
  expect_true(one_sided.artifacts.portfolio_summary_json.find("\"mark_available\": false") != std::string::npos,
              "one-sided artifact encodes unavailable mark");
}

void test_historical_book_immutability() {
  const auto result = run_example("build/book_immutability_run");
  const replay::FeedParserConfig config{.tick_size = "0.01", .price_format = replay::PriceFieldFormat::Ticks};
  const auto book_feed = replay::load_book_updates_csv("tests/golden/e2e_book_updates.csv", config);
  replay::OrderBook book;
  for (const auto& event : book_feed.events()) {
    book.apply(event.book_update());
  }
  expect_true(result.final_book.state_hash() == book.state_hash(), "strategy execution does not mutate historical book");
}

}  // namespace

int main() {
  test_valid_end_to_end_run_and_golden_outputs();
  test_repeatability_and_output_directory_independence();
  test_same_content_different_source_path();
  test_config_changes();
  test_error_paths_and_output_policy();
  test_empty_feed_and_mark_unavailable_cases();
  test_historical_book_immutability();

  if (failures != 0) {
    std::cerr << failures << " replay engine test failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
