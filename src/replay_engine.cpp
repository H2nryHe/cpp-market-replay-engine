#include "replay/replay_engine.hpp"

#include "queue_imbalance_strategy.hpp"
#include "replay/execution_simulator.hpp"
#include "replay/latency_execution.hpp"
#include "replay/market_feed.hpp"
#include "replay/strategy.hpp"
#include "replay/version.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace replay {
namespace {

constexpr std::uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t fnv1a_prime = 1099511628211ULL;

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t' || text[end - 1U] == '\r')) {
    --end;
  }
  return std::string{text.substr(begin, end - begin)};
}

bool is_blank_or_comment(std::string_view line) {
  const auto cleaned = trim(line);
  return cleaned.empty() || cleaned.front() == '#';
}

std::vector<std::string> split(std::string_view text, char delimiter) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const auto pos = text.find(delimiter, begin);
    const auto end = pos == std::string_view::npos ? text.size() : pos;
    fields.push_back(trim(text.substr(begin, end - begin)));
    if (pos == std::string_view::npos) {
      break;
    }
    begin = pos + 1U;
  }
  return fields;
}

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = fnv1a_offset_basis;
  for (const char ch : text) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
    hash *= fnv1a_prime;
  }
  return hash;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream os;
  os << std::hex << std::setfill('0') << std::setw(16) << value;
  return os.str();
}

template <typename Integer>
Integer parse_integer(std::string_view text, std::string_view field) {
  if (text.empty()) {
    throw std::invalid_argument(std::string{field} + " is required");
  }
  Integer value{};
  const auto* const begin = text.data();
  const auto* const end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec == std::errc::result_out_of_range) {
    throw std::invalid_argument(std::string{field} + " is out of range");
  }
  if (result.ec != std::errc{} || result.ptr != end) {
    throw std::invalid_argument(std::string{field} + " must be an integer");
  }
  return value;
}

double parse_double_value(std::string_view text, std::string_view field) {
  if (text.empty()) {
    throw std::invalid_argument(std::string{field} + " is required");
  }
  std::string copy{text};
  std::size_t parsed = 0;
  double value = 0.0;
  try {
    value = std::stod(copy, &parsed);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string{field} + " must be a decimal number");
  }
  if (parsed != copy.size()) {
    throw std::invalid_argument(std::string{field} + " must be a decimal number");
  }
  return value;
}

bool parse_bool(std::string_view text, std::string_view field) {
  if (text == "true" || text == "1") {
    return true;
  }
  if (text == "false" || text == "0") {
    return false;
  }
  throw std::invalid_argument(std::string{field} + " must be true/false");
}

std::string side_name(Side side) {
  switch (side) {
    case Side::Buy:
      return "buy";
    case Side::Sell:
      return "sell";
  }
  throw std::invalid_argument("invalid Side");
}

std::string order_type_name(OrderType order_type) {
  switch (order_type) {
    case OrderType::Market:
      return "market";
    case OrderType::Limit:
      return "limit";
  }
  throw std::invalid_argument("invalid OrderType");
}

std::string price_format_name(PriceFieldFormat price_format) {
  switch (price_format) {
    case PriceFieldFormat::Decimal:
      return "decimal";
    case PriceFieldFormat::Ticks:
      return "ticks";
  }
  throw std::invalid_argument("invalid PriceFieldFormat");
}

PriceFieldFormat parse_price_format(std::string_view text) {
  if (text == "decimal") {
    return PriceFieldFormat::Decimal;
  }
  if (text == "ticks") {
    return PriceFieldFormat::Ticks;
  }
  throw std::invalid_argument("price_format must be decimal or ticks");
}

OrderType parse_order_type_config(std::string_view text) {
  return parse_order_type(text);
}

std::string optional_int64_string(std::optional<PriceTicks> value) {
  if (!value.has_value()) {
    return "";
  }
  return std::to_string(*value);
}

std::string optional_latency_string(std::optional<LatencyNs> value) {
  if (!value.has_value()) {
    return "";
  }
  return std::to_string(value->count);
}

std::string canonical_scripted_intents(const std::vector<ScriptedIntentConfig>& intents) {
  std::ostringstream os;
  for (const auto& intent : intents) {
    os << intent.trigger_key.timestamp_ns << ':' << intent.trigger_key.sequence_id << ':' << side_name(intent.side)
       << ':' << order_type_name(intent.order_type) << ':' << intent.quantity << ':'
       << optional_int64_string(intent.limit_price_ticks) << ';';
  }
  return os.str();
}

ScriptedIntentConfig parse_scripted_intent(std::string_view text) {
  const auto fields = split(text, ':');
  if (fields.size() != 6U) {
    throw std::invalid_argument("scripted_intents entries must be timestamp:sequence:side:order_type:quantity:limit_price");
  }
  auto order_type = parse_order_type_config(fields[3]);
  std::optional<PriceTicks> limit_price_ticks;
  if (!fields[5].empty()) {
    limit_price_ticks = parse_integer<PriceTicks>(fields[5], "scripted limit_price_ticks");
    validate_price_ticks(*limit_price_ticks);
  }
  if (order_type == OrderType::Limit && !limit_price_ticks.has_value()) {
    throw std::invalid_argument("scripted limit order requires limit_price_ticks");
  }
  if (order_type == OrderType::Market && limit_price_ticks.has_value()) {
    throw std::invalid_argument("scripted market order must not set limit_price_ticks");
  }
  const auto quantity = parse_integer<Quantity>(fields[4], "scripted quantity");
  validate_quantity(quantity);
  if (quantity <= 0) {
    throw std::invalid_argument("scripted quantity must be positive");
  }
  return ScriptedIntentConfig{
      .trigger_key =
          EventKey{parse_integer<TimestampNs>(fields[0], "scripted timestamp_ns"),
                   parse_integer<std::uint64_t>(fields[1], "scripted sequence_id")},
      .side = parse_side(fields[2]),
      .order_type = order_type,
      .quantity = quantity,
      .limit_price_ticks = limit_price_ticks,
  };
}

std::vector<ScriptedIntentConfig> parse_scripted_intents(std::string_view text) {
  std::vector<ScriptedIntentConfig> intents;
  if (trim(text).empty()) {
    return intents;
  }
  for (const auto& entry : split(text, ';')) {
    if (!entry.empty()) {
      intents.push_back(parse_scripted_intent(entry));
    }
  }
  std::sort(intents.begin(), intents.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.trigger_key == rhs.trigger_key) {
      return order_type_name(lhs.order_type) < order_type_name(rhs.order_type);
    }
    return lhs.trigger_key < rhs.trigger_key;
  });
  return intents;
}

std::map<std::string, std::string> parse_key_value_file(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) {
    throw std::runtime_error{"failed to open config file: " + path.string()};
  }
  std::map<std::string, std::string> values;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (is_blank_or_comment(line)) {
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      throw std::invalid_argument("config line " + std::to_string(line_number) + " is missing '='");
    }
    const auto key = trim(std::string_view{line}.substr(0, equals));
    const auto value = trim(std::string_view{line}.substr(equals + 1U));
    if (key.empty()) {
      throw std::invalid_argument("config line " + std::to_string(line_number) + " has empty key");
    }
    if (!values.emplace(key, value).second) {
      throw std::invalid_argument("duplicate config key: " + key);
    }
  }
  return values;
}

const std::string& required_value(const std::map<std::string, std::string>& values, const std::string& key) {
  const auto found = values.find(key);
  if (found == values.end() || found->second.empty()) {
    throw std::invalid_argument("missing required config key: " + key);
  }
  return found->second;
}

std::string optional_value(const std::map<std::string, std::string>& values,
                           const std::string& key,
                           std::string default_value) {
  const auto found = values.find(key);
  if (found == values.end()) {
    return default_value;
  }
  return found->second;
}

LatencyNs parse_latency_ns_config(std::string_view text, std::string_view field) {
  const auto value = parse_integer<std::int64_t>(text, field);
  return LatencyNs::from_nanoseconds(value);
}

void validate_replay_config(const ReplayConfig& config) {
  validate_tick_size(config.feed_parser.tick_size);
  if (config.book_updates_path.empty()) {
    throw std::invalid_argument("book_updates_path is required");
  }
  if (config.trades_path.empty()) {
    throw std::invalid_argument("trades_path is required");
  }
  if (config.output_directory.empty()) {
    throw std::invalid_argument("output_directory is required");
  }
  if (config.strategy_type != "queue_imbalance" && config.strategy_type != "scripted") {
    throw std::invalid_argument("strategy_type must be queue_imbalance or scripted");
  }
  if (config.strategy_order_quantity <= 0) {
    throw std::invalid_argument("strategy_order_quantity must be positive");
  }
  if (config.strategy_order_type == OrderType::Limit && !config.strategy_limit_price_ticks.has_value() &&
      config.strategy_type == "queue_imbalance") {
    throw std::invalid_argument("queue_imbalance limit strategy requires strategy_limit_price_ticks");
  }
  if (config.queue_imbalance_sell_threshold > config.queue_imbalance_buy_threshold) {
    throw std::invalid_argument("queue_imbalance_sell_threshold must be <= queue_imbalance_buy_threshold");
  }
  static_cast<void>(QueueFraction{config.queue_fraction_ppm});
  static_cast<void>(FeeModel{config.fee_rate_ppm});
  if (config.strategy_type == "scripted" && config.scripted_intents.empty()) {
    throw std::invalid_argument("scripted strategy requires scripted_intents");
  }
}

std::vector<MarketEvent> merge_events(const NormalizedMarketFeed& book_updates, const NormalizedMarketFeed& trades) {
  std::vector<MarketEvent> events;
  events.reserve(book_updates.size() + trades.size());
  events.insert(events.end(), book_updates.events().begin(), book_updates.events().end());
  events.insert(events.end(), trades.events().begin(), trades.events().end());
  std::stable_sort(events.begin(), events.end(), [](const MarketEvent& lhs, const MarketEvent& rhs) {
    if (lhs.key() == rhs.key()) {
      return lhs.type() == MarketEventType::BookUpdate && rhs.type() == MarketEventType::Trade;
    }
    return lhs.key() < rhs.key();
  });
  for (std::size_t index = 1; index < events.size(); ++index) {
    if (events[index - 1U].key() == events[index].key()) {
      throw std::invalid_argument("duplicate global market event key across input feeds");
    }
  }
  return events;
}

class CountingExecutionIntentSink final : public IntentSink {
 public:
  CountingExecutionIntentSink(LatencyAwareExecution& execution, EventLoop& loop, ReplayMetrics& metrics)
      : execution_{execution}, loop_{loop}, metrics_{metrics} {}

  void emit(OrderIntent intent) override {
    ++metrics_.strategy_intents;
    static_cast<void>(execution_.submit_order_intent(intent, loop_));
  }

 private:
  LatencyAwareExecution& execution_;
  EventLoop& loop_;
  ReplayMetrics& metrics_;
};

class ScriptedReplayStrategy final : public Strategy {
 public:
  explicit ScriptedReplayStrategy(std::vector<ScriptedIntentConfig> intents) : intents_{std::move(intents)} {}

  void on_book(const BookUpdateEvent& event, const OrderBook&, IntentSink& intents) override {
    emit_for_key(event.key, intents);
  }

  void on_trade(const TradeEvent& event, const OrderBook&, IntentSink& intents) override {
    emit_for_key(event.key, intents);
  }

  void on_timer(TimestampNs, const InternalEvent&, const OrderBook&, IntentSink&) override {}

 private:
  void emit_for_key(EventKey key, IntentSink& sink) {
    for (std::size_t index = 0; index < intents_.size(); ++index) {
      if (!emitted_[index] && intents_[index].trigger_key == key) {
        emitted_[index] = true;
        sink.emit(OrderIntent{.side = intents_[index].side,
                              .quantity = intents_[index].quantity,
                              .order_type = intents_[index].order_type,
                              .limit_price_ticks = intents_[index].limit_price_ticks,
                              .decision_timestamp_ns = key.timestamp_ns});
      }
    }
  }

  std::vector<ScriptedIntentConfig> intents_;
  std::vector<bool> emitted_ = std::vector<bool>(intents_.size(), false);
};

std::unique_ptr<Strategy> make_strategy(const ReplayConfig& config) {
  if (config.strategy_type == "scripted") {
    return std::make_unique<ScriptedReplayStrategy>(config.scripted_intents);
  }
  return std::make_unique<QueueImbalanceStrategy>(QueueImbalanceConfig{
      .depth_levels = config.queue_imbalance_depth_levels,
      .buy_threshold = config.queue_imbalance_buy_threshold,
      .sell_threshold = config.queue_imbalance_sell_threshold,
      .order_quantity = config.strategy_order_quantity,
      .order_type = config.strategy_order_type,
      .limit_price_ticks = config.strategy_limit_price_ticks,
  });
}

void apply_new_fills(const LatencyAwareExecution& execution, std::size_t& applied_fill_count, Portfolio& portfolio) {
  const auto& fills = execution.fills();
  while (applied_fill_count < fills.size()) {
    portfolio.apply_fill(fills[applied_fill_count]);
    ++applied_fill_count;
  }
}

bool is_active_resting_order(const Order& order) {
  return order.order_type() == OrderType::Limit &&
         (order.status() == OrderStatus::Acknowledged || order.status() == OrderStatus::PartiallyFilled);
}

void count_order_statuses(const std::vector<Order>& orders, ReplayMetrics& metrics) {
  for (const auto& order : orders) {
    if (order.status() == OrderStatus::Filled) {
      ++metrics.orders_filled;
    } else if (order.status() == OrderStatus::PartiallyFilled) {
      ++metrics.orders_partially_filled;
    } else if (order.status() == OrderStatus::Canceled) {
      ++metrics.orders_canceled;
    } else if (order.status() == OrderStatus::Rejected) {
      ++metrics.orders_rejected;
    }
  }
}

std::string json_string(std::string_view text) {
  std::ostringstream os;
  os << '"';
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        os << "\\\\";
        break;
      case '"':
        os << "\\\"";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        os << ch;
        break;
    }
  }
  os << '"';
  return os.str();
}

std::string json_optional_i64(std::optional<std::int64_t> value) {
  if (!value.has_value()) {
    return "null";
  }
  return std::to_string(*value);
}

std::string json_optional_mark_value(const std::optional<PortfolioMark>& mark,
                                     AccountingAmountX2 PortfolioMark::*member) {
  if (!mark.has_value()) {
    return "null";
  }
  return std::to_string((*mark).*member);
}

std::string orders_csv(const std::vector<Order>& orders) {
  auto sorted = orders;
  std::sort(sorted.begin(), sorted.end(), [](const Order& lhs, const Order& rhs) {
    return lhs.order_id() < rhs.order_id();
  });
  std::ostringstream os;
  os << "order_id,side,order_type,original_quantity,filled_quantity,remaining_quantity,limit_price_ticks,"
        "decision_time_ns,submit_time_ns,exchange_arrival_time_ns,final_status\n";
  for (const auto& order : sorted) {
    os << order.order_id() << ',' << side_name(order.side()) << ',' << order_type_name(order.order_type()) << ','
       << order.original_quantity() << ',' << order.filled_quantity() << ',' << order.remaining_quantity() << ','
       << optional_int64_string(order.limit_price_ticks()) << ',' << order.decision_timestamp_ns() << ','
       << order.submit_timestamp_ns() << ',' << order.exchange_arrival_timestamp_ns() << ','
       << order_status_name(order.status()) << '\n';
  }
  return os.str();
}

std::string fills_csv(const std::vector<Fill>& fills) {
  std::ostringstream os;
  os << "fill_sequence_id,order_id,timestamp_ns,side,price_ticks,quantity,fee\n";
  for (const auto& fill : fills) {
    os << fill.fill_sequence_id << ',' << fill.order_id << ',' << fill.fill_timestamp_ns << ','
       << side_name(fill.side) << ',' << fill.price_ticks << ',' << fill.quantity << ',' << fill.fee_amount << '\n';
  }
  return os.str();
}

std::string ledger_csv(const Portfolio& portfolio) {
  std::ostringstream os;
  os << "fill_sequence_id,order_id,timestamp_ns,side,price_ticks,quantity,fee,cash_after,inventory_after,"
        "realized_gross_pnl_after\n";
  for (const auto& entry : portfolio.ledger()) {
    os << entry.fill_sequence_id << ',' << entry.order_id << ',' << entry.fill_timestamp_ns << ','
       << side_name(entry.side) << ',' << entry.price_ticks << ',' << entry.quantity << ',' << entry.fee_amount << ','
       << entry.cash_after << ',' << entry.inventory_after << ',' << entry.realized_gross_pnl_after << '\n';
  }
  return os.str();
}

std::string portfolio_summary_json(const Portfolio& portfolio, const std::optional<PortfolioMark>& mark) {
  const bool equity_determinable = mark.has_value();
  const bool mark_available = mark.has_value() && mark->mid_price_x2_ticks.has_value();
  std::ostringstream os;
  os << "{\n";
  os << "  \"initial_cash\": " << portfolio.initial_cash() << ",\n";
  os << "  \"ending_cash\": " << portfolio.cash() << ",\n";
  os << "  \"ending_inventory\": " << portfolio.inventory() << ",\n";
  os << "  \"realized_gross_pnl\": " << portfolio.realized_gross_pnl() << ",\n";
  os << "  \"unrealized_gross_pnl_x2\": " << json_optional_mark_value(mark, &PortfolioMark::unrealized_gross_pnl_x2)
     << ",\n";
  os << "  \"total_fees\": " << portfolio.total_fees() << ",\n";
  os << "  \"net_total_pnl_x2\": " << json_optional_mark_value(mark, &PortfolioMark::net_total_pnl_x2) << ",\n";
  os << "  \"turnover\": " << portfolio.turnover() << ",\n";
  os << "  \"fill_count\": " << portfolio.fill_count() << ",\n";
  os << "  \"ending_equity_x2\": " << json_optional_mark_value(mark, &PortfolioMark::equity_x2) << ",\n";
  os << "  \"equity_determinable\": " << (equity_determinable ? "true" : "false") << ",\n";
  os << "  \"mark_available\": " << (mark_available ? "true" : "false") << ",\n";
  os << "  \"final_mid_x2\": " << (mark.has_value() ? json_optional_i64(mark->mid_price_x2_ticks) : "null") << "\n";
  os << "}\n";
  return os.str();
}

std::string metrics_json(const ReplayMetrics& metrics) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"market_events_processed\": " << metrics.market_events_processed << ",\n";
  os << "  \"book_updates_processed\": " << metrics.book_updates_processed << ",\n";
  os << "  \"trades_processed\": " << metrics.trades_processed << ",\n";
  os << "  \"internal_events_processed\": " << metrics.internal_events_processed << ",\n";
  os << "  \"strategy_intents\": " << metrics.strategy_intents << ",\n";
  os << "  \"orders_submitted\": " << metrics.orders_submitted << ",\n";
  os << "  \"orders_filled\": " << metrics.orders_filled << ",\n";
  os << "  \"orders_partially_filled\": " << metrics.orders_partially_filled << ",\n";
  os << "  \"orders_canceled\": " << metrics.orders_canceled << ",\n";
  os << "  \"orders_rejected\": " << metrics.orders_rejected << ",\n";
  os << "  \"fills\": " << metrics.fills << "\n";
  os << "}\n";
  return os.str();
}

std::string run_manifest_json(const ReplayRunResult& result) {
  const auto& mark = result.final_mark;
  std::ostringstream os;
  os << "{\n";
  os << "  \"engine_name\": " << json_string(engine_name()) << ",\n";
  os << "  \"engine_version\": " << json_string(engine_version()) << ",\n";
  os << "  \"git_commit\": " << json_string("unavailable") << ",\n";
  os << "  \"book_updates_hash\": " << json_string(result.hashes.book_updates_hash) << ",\n";
  os << "  \"trades_hash\": " << json_string(result.hashes.trades_hash) << ",\n";
  os << "  \"input_hash\": " << json_string(result.hashes.input_hash) << ",\n";
  os << "  \"config_hash\": " << json_string(result.hashes.config_hash) << ",\n";
  os << "  \"processed_market_event_count\": " << result.metrics.market_events_processed << ",\n";
  os << "  \"book_update_count\": " << result.metrics.book_updates_processed << ",\n";
  os << "  \"trade_count\": " << result.metrics.trades_processed << ",\n";
  os << "  \"internal_event_count\": " << result.metrics.internal_events_processed << ",\n";
  os << "  \"strategy_intent_count\": " << result.metrics.strategy_intents << ",\n";
  os << "  \"submitted_order_count\": " << result.metrics.orders_submitted << ",\n";
  os << "  \"fill_count\": " << result.metrics.fills << ",\n";
  os << "  \"start_simulation_timestamp_ns\": " << result.start_timestamp_ns << ",\n";
  os << "  \"end_simulation_timestamp_ns\": " << result.end_timestamp_ns << ",\n";
  os << "  \"ending_inventory\": " << result.portfolio.inventory() << ",\n";
  os << "  \"total_fees\": " << result.portfolio.total_fees() << ",\n";
  os << "  \"realized_gross_pnl\": " << result.portfolio.realized_gross_pnl() << ",\n";
  os << "  \"unrealized_gross_pnl_x2\": " << json_optional_mark_value(mark, &PortfolioMark::unrealized_gross_pnl_x2)
     << ",\n";
  os << "  \"ending_equity_x2\": " << json_optional_mark_value(mark, &PortfolioMark::equity_x2) << ",\n";
  os << "  \"mark_available\": "
     << (mark.has_value() && mark->mid_price_x2_ticks.has_value() ? "true" : "false") << ",\n";
  os << "  \"final_book_hash\": " << json_string(result.hashes.final_book_hash) << ",\n";
  os << "  \"orders_hash\": " << json_string(result.hashes.orders_hash) << ",\n";
  os << "  \"fills_hash\": " << json_string(result.hashes.fills_hash) << ",\n";
  os << "  \"portfolio_hash\": " << json_string(result.hashes.portfolio_hash) << ",\n";
  os << "  \"metrics_hash\": " << json_string(result.hashes.metrics_hash) << ",\n";
  os << "  \"run_hash\": " << json_string(result.hashes.run_hash) << "\n";
  os << "}\n";
  return os.str();
}

std::string canonical_run_hash_input(const ReplayHashes& hashes) {
  std::ostringstream os;
  os << "engine_version=" << engine_version() << '\n';
  os << "input_hash=" << hashes.input_hash << '\n';
  os << "config_hash=" << hashes.config_hash << '\n';
  os << "final_book_hash=" << hashes.final_book_hash << '\n';
  os << "orders_hash=" << hashes.orders_hash << '\n';
  os << "fills_hash=" << hashes.fills_hash << '\n';
  os << "portfolio_hash=" << hashes.portfolio_hash << '\n';
  os << "metrics_hash=" << hashes.metrics_hash << '\n';
  return os.str();
}

TimestampNs start_timestamp(const std::vector<MarketEvent>& events) {
  if (events.empty()) {
    return 0;
  }
  return events.front().key().timestamp_ns;
}

bool has_run_artifact(const std::filesystem::path& directory) {
  static constexpr std::string_view names[] = {
      "run_manifest.json", "orders.csv", "fills.csv", "portfolio_summary.json", "metrics.json"};
  for (const auto name : names) {
    if (std::filesystem::exists(directory / name)) {
      return true;
    }
  }
  return false;
}

void write_text_file(const std::filesystem::path& path, std::string_view content) {
  std::ofstream output{path, std::ios::binary};
  if (!output) {
    throw std::runtime_error{"failed to open artifact for write: " + path.filename().string()};
  }
  output << content;
  if (!output) {
    throw std::runtime_error{"failed to write artifact: " + path.filename().string()};
  }
}

std::optional<PortfolioMark> final_portfolio_mark(const Portfolio& portfolio, const OrderBook& book) {
  const auto mark = portfolio.mark_to_market(book);
  if (mark.has_value()) {
    return mark;
  }
  return std::nullopt;
}

}  // namespace

std::string hash_text(std::string_view text) {
  return hex64(fnv1a64(text));
}

std::string hash_file_content(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    throw std::runtime_error{"failed to open input file: " + path.string()};
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (input.bad()) {
    throw std::runtime_error{"failed to read input file: " + path.string()};
  }
  return hash_text(content.str());
}

ReplayConfig load_replay_config(const std::filesystem::path& path) {
  const auto values = parse_key_value_file(path);
  ReplayConfig config;
  config.book_updates_path = required_value(values, "book_updates_path");
  config.trades_path = required_value(values, "trades_path");
  config.output_directory = optional_value(values, "output_directory", "");
  config.feed_parser.tick_size = optional_value(values, "tick_size", "0.01");
  config.feed_parser.price_format = parse_price_format(optional_value(values, "price_format", "decimal"));
  config.strategy_type = optional_value(values, "strategy_type", "queue_imbalance");
  config.queue_imbalance_depth_levels =
      parse_integer<std::size_t>(optional_value(values, "queue_imbalance_depth_levels", "1"),
                                 "queue_imbalance_depth_levels");
  config.queue_imbalance_buy_threshold =
      parse_double_value(optional_value(values, "queue_imbalance_buy_threshold", "0.25"),
                         "queue_imbalance_buy_threshold");
  config.queue_imbalance_sell_threshold =
      parse_double_value(optional_value(values, "queue_imbalance_sell_threshold", "-0.25"),
                         "queue_imbalance_sell_threshold");
  config.strategy_order_quantity =
      parse_integer<Quantity>(optional_value(values, "strategy_order_quantity", "1"), "strategy_order_quantity");
  config.strategy_order_type =
      parse_order_type_config(optional_value(values, "strategy_order_type", "market"));
  const auto limit = optional_value(values, "strategy_limit_price_ticks", "");
  if (!limit.empty()) {
    config.strategy_limit_price_ticks = parse_integer<PriceTicks>(limit, "strategy_limit_price_ticks");
    validate_price_ticks(*config.strategy_limit_price_ticks);
  }
  config.order_latency =
      parse_latency_ns_config(optional_value(values, "order_latency_ns", "0"), "order_latency_ns");
  const auto cancel_after = optional_value(values, "cancel_after_arrival_ns", "");
  if (!cancel_after.empty()) {
    config.cancel_after_arrival = parse_latency_ns_config(cancel_after, "cancel_after_arrival_ns");
  }
  config.queue_fraction_ppm =
      parse_integer<std::int64_t>(optional_value(values, "queue_fraction_ppm", "0"), "queue_fraction_ppm");
  const auto queue_fraction_decimal = optional_value(values, "queue_fraction", "");
  if (!queue_fraction_decimal.empty()) {
    const auto parsed = parse_double_value(queue_fraction_decimal, "queue_fraction");
    if (parsed < 0.0 || parsed > 1.0) {
      throw std::invalid_argument("queue_fraction must be in [0, 1]");
    }
    config.queue_fraction_ppm = static_cast<std::int64_t>((parsed * 1'000'000.0) + 0.5);
  }
  config.fee_rate_ppm =
      parse_integer<std::int64_t>(optional_value(values, "fee_rate_ppm", "0"), "fee_rate_ppm");
  config.initial_cash =
      parse_integer<AccountingAmount>(optional_value(values, "initial_cash", "0"), "initial_cash");
  config.scripted_intents = parse_scripted_intents(optional_value(values, "scripted_intents", ""));
  static_cast<void>(parse_bool(optional_value(values, "deterministic", "true"), "deterministic"));
  validate_replay_config(config);
  return config;
}

std::string canonical_replay_config(const ReplayConfig& config) {
  std::ostringstream os;
  os << "tick_size=" << config.feed_parser.tick_size << '\n';
  os << "price_format=" << price_format_name(config.feed_parser.price_format) << '\n';
  os << "strategy_type=" << config.strategy_type << '\n';
  os << "queue_imbalance_depth_levels=" << config.queue_imbalance_depth_levels << '\n';
  os << "queue_imbalance_buy_threshold=" << config.queue_imbalance_buy_threshold << '\n';
  os << "queue_imbalance_sell_threshold=" << config.queue_imbalance_sell_threshold << '\n';
  os << "strategy_order_quantity=" << config.strategy_order_quantity << '\n';
  os << "strategy_order_type=" << order_type_name(config.strategy_order_type) << '\n';
  os << "strategy_limit_price_ticks=" << optional_int64_string(config.strategy_limit_price_ticks) << '\n';
  os << "order_latency_ns=" << config.order_latency.count << '\n';
  os << "cancel_after_arrival_ns=" << optional_latency_string(config.cancel_after_arrival) << '\n';
  os << "queue_fraction_ppm=" << config.queue_fraction_ppm << '\n';
  os << "fee_rate_ppm=" << config.fee_rate_ppm << '\n';
  os << "initial_cash=" << config.initial_cash << '\n';
  os << "scripted_intents=" << canonical_scripted_intents(config.scripted_intents) << '\n';
  return os.str();
}

ReplayEngine::ReplayEngine(ReplayConfig config) : config_{std::move(config)} {
  validate_replay_config(config_);
}

ReplayRunResult ReplayEngine::run() const {
  const auto book_hash = hash_file_content(config_.book_updates_path);
  const auto trades_hash = hash_file_content(config_.trades_path);
  const auto book_feed = load_book_updates_csv(config_.book_updates_path, config_.feed_parser);
  const auto trade_feed = load_trades_csv(config_.trades_path, config_.feed_parser);
  const auto events = merge_events(book_feed, trade_feed);

  ReplayMetrics metrics;
  for (const auto& event : events) {
    if (event.type() == MarketEventType::BookUpdate) {
      ++metrics.book_updates_processed;
    } else {
      ++metrics.trades_processed;
    }
  }
  metrics.market_events_processed = events.size();

  OrderBook book;
  EventLoop loop{events};
  LatencyAwareExecution execution{LatencyExecutionConfig{
      .order_latency = config_.order_latency,
      .execution_config = ExecutionConfig{.fee_model = FeeModel{config_.fee_rate_ppm}},
      .queue_fraction_ppm = config_.queue_fraction_ppm,
      .cancel_latency = LatencyNs{},
  }};
  Portfolio portfolio{config_.initial_cash};
  auto strategy = make_strategy(config_);
  std::size_t applied_fill_count = 0;

  auto loop_result = loop.run(EventLoopHandlers{
      .order_book = &book,
      .on_market =
          [&book, &execution, &metrics, &portfolio, &strategy, &applied_fill_count](const MarketEvent& event,
                                                                                    EventLoop& loop_ref) {
            CountingExecutionIntentSink intents{execution, loop_ref, metrics};
            if (event.type() == MarketEventType::BookUpdate) {
              strategy->on_book(event.book_update(), book, intents);
            } else {
              execution.process_trade(event.trade());
              apply_new_fills(execution, applied_fill_count, portfolio);
              strategy->on_trade(event.trade(), book, intents);
            }
          },
      .on_internal =
          [&book, &execution, &metrics, &portfolio, &strategy, &applied_fill_count, this](const ScheduledInternalEvent& event,
                                                                                         EventLoop& loop_ref) {
            if (event.event.type == InternalEventType::OrderArrival) {
              const auto& result = execution.process_order_arrival(event, book);
              apply_new_fills(execution, applied_fill_count, portfolio);
              if (config_.cancel_after_arrival.has_value() && is_active_resting_order(result.order)) {
                static_cast<void>(
                    execution.request_cancel(result.order.order_id(), *config_.cancel_after_arrival, loop_ref));
              }
              return;
            }
            if (event.event.type == InternalEventType::CancelArrival) {
              execution.process_cancel_arrival(event);
              return;
            }
            if (event.event.type == InternalEventType::Timer) {
              CountingExecutionIntentSink intents{execution, loop_ref, metrics};
              strategy->on_timer(event.timestamp_ns, event.event, book, intents);
            }
          },
  });

  metrics.internal_events_processed = loop_result.processed_event_count - metrics.market_events_processed;
  auto final_orders = execution.final_orders();
  metrics.orders_submitted = final_orders.size();
  metrics.fills = execution.fills().size();
  count_order_statuses(final_orders, metrics);

  ReplayRunResult result;
  result.config = config_;
  result.event_loop_result = std::move(loop_result);
  result.final_book = book;
  result.portfolio = portfolio;
  result.final_mark = final_portfolio_mark(result.portfolio, result.final_book);
  result.final_orders = std::move(final_orders);
  result.fills = execution.fills();
  result.metrics = metrics;
  result.start_timestamp_ns = start_timestamp(events);
  result.end_timestamp_ns = result.event_loop_result.final_time_ns;

  result.hashes.book_updates_hash = book_hash;
  result.hashes.trades_hash = trades_hash;
  result.hashes.input_hash = hash_text("book_updates=" + book_hash + "\ntrades=" + trades_hash + "\n");
  result.hashes.config_hash = hash_text(canonical_replay_config(config_));
  result.hashes.final_book_hash = hex64(result.final_book.state_hash());
  result.artifacts.orders_csv = orders_csv(result.final_orders);
  result.artifacts.fills_csv = fills_csv(result.fills);
  result.artifacts.ledger_csv = ledger_csv(result.portfolio);
  result.artifacts.portfolio_summary_json = portfolio_summary_json(result.portfolio, result.final_mark);
  result.artifacts.metrics_json = metrics_json(result.metrics);
  result.hashes.orders_hash = hash_text(result.artifacts.orders_csv);
  result.hashes.fills_hash = hash_text(result.artifacts.fills_csv);
  result.hashes.portfolio_hash = hash_text(result.artifacts.portfolio_summary_json);
  result.hashes.metrics_hash = hash_text(result.artifacts.metrics_json);
  result.hashes.run_hash = hash_text(canonical_run_hash_input(result.hashes));
  result.artifacts.run_manifest_json = run_manifest_json(result);
  return result;
}

void write_replay_artifacts(const ReplayRunResult& result,
                            const std::filesystem::path& output_directory,
                            bool force) {
  if (output_directory.empty()) {
    throw std::invalid_argument("output directory is required");
  }
  if (std::filesystem::exists(output_directory) && !std::filesystem::is_directory(output_directory)) {
    throw std::runtime_error("output path exists and is not a directory");
  }
  if (std::filesystem::exists(output_directory) && has_run_artifact(output_directory) && !force) {
    throw std::runtime_error("output directory already contains run artifacts; pass --force to replace it");
  }
  if (!output_directory.parent_path().empty() && !std::filesystem::exists(output_directory.parent_path())) {
    throw std::runtime_error("output directory parent does not exist");
  }

  const auto temporary = std::filesystem::path{output_directory.string() + ".tmp"};
  if (std::filesystem::exists(temporary)) {
    std::filesystem::remove_all(temporary);
  }
  std::filesystem::create_directories(temporary);

  try {
    write_text_file(temporary / "orders.csv", result.artifacts.orders_csv);
    write_text_file(temporary / "fills.csv", result.artifacts.fills_csv);
    write_text_file(temporary / "ledger.csv", result.artifacts.ledger_csv);
    write_text_file(temporary / "portfolio_summary.json", result.artifacts.portfolio_summary_json);
    write_text_file(temporary / "metrics.json", result.artifacts.metrics_json);
    write_text_file(temporary / "run_manifest.json", result.artifacts.run_manifest_json);
    if (std::filesystem::exists(output_directory)) {
      if (!force && !std::filesystem::is_empty(output_directory)) {
        throw std::runtime_error("output directory is not empty; pass --force to replace it");
      }
      std::filesystem::remove_all(output_directory);
    }
    std::filesystem::rename(temporary, output_directory);
  } catch (...) {
    std::filesystem::remove_all(temporary);
    throw;
  }
}

ReplayRunResult run_replay_from_config_file(const std::filesystem::path& config_path,
                                            std::optional<std::filesystem::path> output_override,
                                            bool force_output) {
  auto config = load_replay_config(config_path);
  if (output_override.has_value()) {
    config.output_directory = *output_override;
  }
  validate_replay_config(config);
  ReplayEngine engine{config};
  auto result = engine.run();
  write_replay_artifacts(result, config.output_directory, force_output);
  return result;
}

}  // namespace replay
