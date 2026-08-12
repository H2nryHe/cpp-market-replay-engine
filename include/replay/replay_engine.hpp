#pragma once

#include "replay/event_loop.hpp"
#include "replay/fill.hpp"
#include "replay/order.hpp"
#include "replay/order_book.hpp"
#include "replay/portfolio.hpp"
#include "replay/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace replay {

struct ScriptedIntentConfig {
  EventKey trigger_key{};
  Side side{};
  OrderType order_type{OrderType::Market};
  Quantity quantity{};
  std::optional<PriceTicks> limit_price_ticks{};
};

struct ReplayConfig {
  std::filesystem::path book_updates_path{};
  std::filesystem::path trades_path{};
  std::filesystem::path output_directory{};
  FeedParserConfig feed_parser{};
  std::string strategy_type{"queue_imbalance"};
  std::size_t queue_imbalance_depth_levels{1};
  double queue_imbalance_buy_threshold{0.25};
  double queue_imbalance_sell_threshold{-0.25};
  Quantity strategy_order_quantity{1};
  OrderType strategy_order_type{OrderType::Market};
  std::optional<PriceTicks> strategy_limit_price_ticks{};
  LatencyNs order_latency{};
  std::optional<LatencyNs> cancel_after_arrival{};
  std::int64_t queue_fraction_ppm{};
  std::int64_t fee_rate_ppm{};
  AccountingAmount initial_cash{};
  std::vector<ScriptedIntentConfig> scripted_intents{};
};

struct ReplayMetrics {
  std::size_t market_events_processed{};
  std::size_t book_updates_processed{};
  std::size_t trades_processed{};
  std::size_t internal_events_processed{};
  std::size_t strategy_intents{};
  std::size_t orders_submitted{};
  std::size_t orders_filled{};
  std::size_t orders_partially_filled{};
  std::size_t orders_canceled{};
  std::size_t orders_rejected{};
  std::size_t fills{};
};

struct ReplayHashes {
  std::string book_updates_hash{};
  std::string trades_hash{};
  std::string input_hash{};
  std::string config_hash{};
  std::string orders_hash{};
  std::string fills_hash{};
  std::string portfolio_hash{};
  std::string metrics_hash{};
  std::string final_book_hash{};
  std::string run_hash{};
};

struct ReplayArtifacts {
  std::string orders_csv{};
  std::string fills_csv{};
  std::string ledger_csv{};
  std::string portfolio_summary_json{};
  std::string metrics_json{};
  std::string run_manifest_json{};
};

struct ReplayRunResult {
  ReplayConfig config{};
  EventLoopResult event_loop_result{};
  OrderBook final_book{};
  Portfolio portfolio{};
  std::optional<PortfolioMark> final_mark{};
  std::vector<Order> final_orders{};
  std::vector<Fill> fills{};
  ReplayMetrics metrics{};
  ReplayHashes hashes{};
  ReplayArtifacts artifacts{};
  TimestampNs start_timestamp_ns{};
  TimestampNs end_timestamp_ns{};
};

ReplayConfig load_replay_config(const std::filesystem::path& path);
std::string canonical_replay_config(const ReplayConfig& config);

class ReplayEngine {
 public:
  explicit ReplayEngine(ReplayConfig config);

  [[nodiscard]] ReplayRunResult run() const;

 private:
  ReplayConfig config_;
};

ReplayRunResult run_replay_from_config_file(const std::filesystem::path& config_path,
                                            std::optional<std::filesystem::path> output_override,
                                            bool force_output);

void write_replay_artifacts(const ReplayRunResult& result, const std::filesystem::path& output_directory, bool force);

std::string hash_text(std::string_view text);
std::string hash_file_content(const std::filesystem::path& path);

}  // namespace replay
