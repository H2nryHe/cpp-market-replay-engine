#include "replay/replay_engine.hpp"
#include "replay/version.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

std::string side_to_string(replay::Side side) {
  switch (side) {
    case replay::Side::Buy:
      return "buy";
    case replay::Side::Sell:
      return "sell";
  }
  throw std::invalid_argument("invalid Side");
}

std::string order_type_to_string(replay::OrderType order_type) {
  switch (order_type) {
    case replay::OrderType::Market:
      return "market";
    case replay::OrderType::Limit:
      return "limit";
  }
  throw std::invalid_argument("invalid OrderType");
}

std::string price_format_to_string(replay::PriceFieldFormat price_format) {
  switch (price_format) {
    case replay::PriceFieldFormat::Decimal:
      return "decimal";
    case replay::PriceFieldFormat::Ticks:
      return "ticks";
  }
  throw std::invalid_argument("invalid PriceFieldFormat");
}

replay::PriceFieldFormat parse_price_format_string(const std::string& value) {
  if (value == "decimal") {
    return replay::PriceFieldFormat::Decimal;
  }
  if (value == "ticks") {
    return replay::PriceFieldFormat::Ticks;
  }
  throw std::invalid_argument("price_format must be decimal or ticks");
}

py::object optional_i64_to_object(std::optional<std::int64_t> value) {
  if (!value.has_value()) {
    return py::none();
  }
  return py::int_(*value);
}

py::object optional_u64_to_object(std::optional<replay::LatencyNs> value) {
  if (!value.has_value()) {
    return py::none();
  }
  return py::int_(value->count);
}

std::optional<replay::LatencyNs> object_to_optional_latency(const py::object& value) {
  if (value.is_none()) {
    return std::nullopt;
  }
  return replay::LatencyNs::from_nanoseconds(value.cast<std::int64_t>());
}

std::optional<replay::PriceTicks> object_to_optional_price_ticks(const py::object& value) {
  if (value.is_none()) {
    return std::nullopt;
  }
  const auto ticks = value.cast<replay::PriceTicks>();
  replay::validate_price_ticks(ticks);
  return ticks;
}

struct PyEventKey {
  replay::TimestampNs timestamp_ns{};
  std::uint64_t sequence_id{};
};

struct PyScriptedIntentConfig {
  PyEventKey trigger_key{};
  replay::Side side{replay::Side::Buy};
  replay::OrderType order_type{replay::OrderType::Market};
  replay::Quantity quantity{1};
  std::optional<replay::PriceTicks> limit_price_ticks{};
};

struct PyOrderSummary {
  replay::OrderId order_id{};
  replay::Side side{};
  std::string side_name{};
  replay::OrderType order_type{};
  std::string order_type_name{};
  replay::Quantity original_quantity{};
  replay::Quantity filled_quantity{};
  replay::Quantity remaining_quantity{};
  std::optional<replay::PriceTicks> limit_price_ticks{};
  replay::TimestampNs decision_timestamp_ns{};
  replay::TimestampNs submit_timestamp_ns{};
  replay::TimestampNs exchange_arrival_timestamp_ns{};
  replay::OrderStatus status{};
  std::string status_name{};
};

struct PyFillSummary {
  replay::FillSequenceId fill_sequence_id{};
  replay::OrderId order_id{};
  replay::TimestampNs timestamp_ns{};
  replay::Side side{};
  std::string side_name{};
  replay::PriceTicks price_ticks{};
  replay::Quantity quantity{};
  replay::FeeAmount fee{};
};

struct PyReplayResult {
  std::size_t processed_event_count{};
  std::size_t book_update_count{};
  std::size_t trade_count{};
  std::size_t internal_event_count{};
  std::size_t strategy_intent_count{};
  std::size_t order_count{};
  std::size_t orders_filled{};
  std::size_t orders_partially_filled{};
  std::size_t orders_canceled{};
  std::size_t orders_rejected{};
  std::size_t fill_count{};
  replay::TimestampNs start_timestamp_ns{};
  replay::TimestampNs end_timestamp_ns{};
  replay::AccountingAmount initial_cash{};
  replay::AccountingAmount ending_cash{};
  replay::Quantity ending_inventory{};
  replay::AccountingAmount realized_gross_pnl{};
  replay::FeeAmount total_fees{};
  replay::AccountingAmount turnover{};
  std::optional<std::int64_t> final_mid_x2{};
  std::optional<std::int64_t> unrealized_gross_pnl_x2{};
  std::optional<std::int64_t> ending_equity_x2{};
  std::optional<std::int64_t> net_total_pnl_x2{};
  bool mark_available{};
  bool equity_determinable{};
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
  std::vector<PyOrderSummary> orders{};
  std::vector<PyFillSummary> fills{};
};

replay::ScriptedIntentConfig to_cpp_intent(const PyScriptedIntentConfig& intent) {
  return replay::ScriptedIntentConfig{
      .trigger_key = replay::EventKey{intent.trigger_key.timestamp_ns, intent.trigger_key.sequence_id},
      .side = intent.side,
      .order_type = intent.order_type,
      .quantity = intent.quantity,
      .limit_price_ticks = intent.limit_price_ticks,
  };
}

PyScriptedIntentConfig to_python_intent(const replay::ScriptedIntentConfig& intent) {
  return PyScriptedIntentConfig{
      .trigger_key = PyEventKey{intent.trigger_key.timestamp_ns, intent.trigger_key.sequence_id},
      .side = intent.side,
      .order_type = intent.order_type,
      .quantity = intent.quantity,
      .limit_price_ticks = intent.limit_price_ticks,
  };
}

std::vector<PyScriptedIntentConfig> get_scripted_intents(const replay::ReplayConfig& config) {
  std::vector<PyScriptedIntentConfig> intents;
  intents.reserve(config.scripted_intents.size());
  for (const auto& intent : config.scripted_intents) {
    intents.push_back(to_python_intent(intent));
  }
  return intents;
}

void set_scripted_intents(replay::ReplayConfig& config, const std::vector<PyScriptedIntentConfig>& intents) {
  config.scripted_intents.clear();
  config.scripted_intents.reserve(intents.size());
  for (const auto& intent : intents) {
    config.scripted_intents.push_back(to_cpp_intent(intent));
  }
}

PyReplayResult to_python_result(const replay::ReplayRunResult& result) {
  PyReplayResult py_result;
  py_result.processed_event_count = result.metrics.market_events_processed;
  py_result.book_update_count = result.metrics.book_updates_processed;
  py_result.trade_count = result.metrics.trades_processed;
  py_result.internal_event_count = result.metrics.internal_events_processed;
  py_result.strategy_intent_count = result.metrics.strategy_intents;
  py_result.order_count = result.metrics.orders_submitted;
  py_result.orders_filled = result.metrics.orders_filled;
  py_result.orders_partially_filled = result.metrics.orders_partially_filled;
  py_result.orders_canceled = result.metrics.orders_canceled;
  py_result.orders_rejected = result.metrics.orders_rejected;
  py_result.fill_count = result.metrics.fills;
  py_result.start_timestamp_ns = result.start_timestamp_ns;
  py_result.end_timestamp_ns = result.end_timestamp_ns;
  py_result.initial_cash = result.portfolio.initial_cash();
  py_result.ending_cash = result.portfolio.cash();
  py_result.ending_inventory = result.portfolio.inventory();
  py_result.realized_gross_pnl = result.portfolio.realized_gross_pnl();
  py_result.total_fees = result.portfolio.total_fees();
  py_result.turnover = result.portfolio.turnover();
  py_result.equity_determinable = result.final_mark.has_value();
  py_result.mark_available = result.final_mark.has_value() && result.final_mark->mid_price_x2_ticks.has_value();
  if (result.final_mark.has_value()) {
    py_result.final_mid_x2 = result.final_mark->mid_price_x2_ticks;
    py_result.unrealized_gross_pnl_x2 = result.final_mark->unrealized_gross_pnl_x2;
    py_result.ending_equity_x2 = result.final_mark->equity_x2;
    py_result.net_total_pnl_x2 = result.final_mark->net_total_pnl_x2;
  }
  py_result.book_updates_hash = result.hashes.book_updates_hash;
  py_result.trades_hash = result.hashes.trades_hash;
  py_result.input_hash = result.hashes.input_hash;
  py_result.config_hash = result.hashes.config_hash;
  py_result.orders_hash = result.hashes.orders_hash;
  py_result.fills_hash = result.hashes.fills_hash;
  py_result.portfolio_hash = result.hashes.portfolio_hash;
  py_result.metrics_hash = result.hashes.metrics_hash;
  py_result.final_book_hash = result.hashes.final_book_hash;
  py_result.run_hash = result.hashes.run_hash;

  py_result.orders.reserve(result.final_orders.size());
  for (const auto& order : result.final_orders) {
    py_result.orders.push_back(PyOrderSummary{
        .order_id = order.order_id(),
        .side = order.side(),
        .side_name = side_to_string(order.side()),
        .order_type = order.order_type(),
        .order_type_name = order_type_to_string(order.order_type()),
        .original_quantity = order.original_quantity(),
        .filled_quantity = order.filled_quantity(),
        .remaining_quantity = order.remaining_quantity(),
        .limit_price_ticks = order.limit_price_ticks(),
        .decision_timestamp_ns = order.decision_timestamp_ns(),
        .submit_timestamp_ns = order.submit_timestamp_ns(),
        .exchange_arrival_timestamp_ns = order.exchange_arrival_timestamp_ns(),
        .status = order.status(),
        .status_name = replay::order_status_name(order.status()),
    });
  }

  py_result.fills.reserve(result.fills.size());
  for (const auto& fill : result.fills) {
    py_result.fills.push_back(PyFillSummary{
        .fill_sequence_id = fill.fill_sequence_id,
        .order_id = fill.order_id,
        .timestamp_ns = fill.fill_timestamp_ns,
        .side = fill.side,
        .side_name = side_to_string(fill.side),
        .price_ticks = fill.price_ticks,
        .quantity = fill.quantity,
        .fee = fill.fee_amount,
    });
  }

  return py_result;
}

PyReplayResult run_engine(const replay::ReplayEngine& engine) {
  replay::ReplayRunResult result;
  {
    py::gil_scoped_release release;
    result = engine.run();
  }
  return to_python_result(result);
}

PyReplayResult run_config_file(const std::string& config_path,
                               const py::object& output_override,
                               bool force_output) {
  std::optional<std::filesystem::path> output_path;
  if (!output_override.is_none()) {
    output_path = output_override.cast<std::string>();
  }

  replay::ReplayRunResult result;
  {
    py::gil_scoped_release release;
    result = replay::run_replay_from_config_file(config_path, output_path, force_output);
  }
  return to_python_result(result);
}

}  // namespace

PYBIND11_MODULE(market_replay, m) {
  m.doc() = "Thin pybind11 bindings for the deterministic C++ market replay engine.";
  m.attr("__version__") = replay::engine_version();

  py::enum_<replay::Side>(m, "Side")
      .value("Buy", replay::Side::Buy)
      .value("Sell", replay::Side::Sell)
      .export_values();

  py::enum_<replay::OrderType>(m, "OrderType")
      .value("Market", replay::OrderType::Market)
      .value("Limit", replay::OrderType::Limit)
      .export_values();

  py::enum_<replay::OrderStatus>(m, "OrderStatus")
      .value("New", replay::OrderStatus::New)
      .value("Pending", replay::OrderStatus::Pending)
      .value("Acknowledged", replay::OrderStatus::Acknowledged)
      .value("PartiallyFilled", replay::OrderStatus::PartiallyFilled)
      .value("Filled", replay::OrderStatus::Filled)
      .value("Canceled", replay::OrderStatus::Canceled)
      .value("Rejected", replay::OrderStatus::Rejected)
      .export_values();

  py::enum_<replay::PriceFieldFormat>(m, "PriceFieldFormat")
      .value("Decimal", replay::PriceFieldFormat::Decimal)
      .value("Ticks", replay::PriceFieldFormat::Ticks)
      .export_values();

  py::class_<PyEventKey>(m, "EventKey")
      .def(py::init<>())
      .def(py::init<replay::TimestampNs, std::uint64_t>(), py::arg("timestamp_ns"), py::arg("sequence_id"))
      .def_readwrite("timestamp_ns", &PyEventKey::timestamp_ns)
      .def_readwrite("sequence_id", &PyEventKey::sequence_id);

  py::class_<PyScriptedIntentConfig>(m, "ScriptedIntent")
      .def(py::init<>())
      .def_readwrite("trigger_key", &PyScriptedIntentConfig::trigger_key)
      .def_readwrite("side", &PyScriptedIntentConfig::side)
      .def_readwrite("order_type", &PyScriptedIntentConfig::order_type)
      .def_readwrite("quantity", &PyScriptedIntentConfig::quantity)
      .def_property(
          "limit_price_ticks",
          [](const PyScriptedIntentConfig& intent) { return optional_i64_to_object(intent.limit_price_ticks); },
          [](PyScriptedIntentConfig& intent, const py::object& value) {
            intent.limit_price_ticks = object_to_optional_price_ticks(value);
          });

  py::class_<replay::ReplayConfig>(m, "ReplayConfig")
      .def(py::init<>())
      .def_static("from_file", [](const std::string& path) { return replay::load_replay_config(path); }, py::arg("path"))
      .def_property(
          "book_updates_path",
          [](const replay::ReplayConfig& config) { return config.book_updates_path.string(); },
          [](replay::ReplayConfig& config, const std::string& path) { config.book_updates_path = path; })
      .def_property(
          "trades_path",
          [](const replay::ReplayConfig& config) { return config.trades_path.string(); },
          [](replay::ReplayConfig& config, const std::string& path) { config.trades_path = path; })
      .def_property(
          "output_directory",
          [](const replay::ReplayConfig& config) { return config.output_directory.string(); },
          [](replay::ReplayConfig& config, const std::string& path) { config.output_directory = path; })
      .def_property(
          "tick_size",
          [](const replay::ReplayConfig& config) { return config.feed_parser.tick_size; },
          [](replay::ReplayConfig& config, const std::string& tick_size) {
            replay::validate_tick_size(tick_size);
            config.feed_parser.tick_size = tick_size;
          })
      .def_property(
          "price_format",
          [](const replay::ReplayConfig& config) { return config.feed_parser.price_format; },
          [](replay::ReplayConfig& config, replay::PriceFieldFormat price_format) {
            config.feed_parser.price_format = price_format;
          })
      .def_property(
          "price_format_name",
          [](const replay::ReplayConfig& config) { return price_format_to_string(config.feed_parser.price_format); },
          [](replay::ReplayConfig& config, const std::string& price_format) {
            config.feed_parser.price_format = parse_price_format_string(price_format);
          })
      .def_readwrite("strategy_type", &replay::ReplayConfig::strategy_type)
      .def_readwrite("queue_imbalance_depth_levels", &replay::ReplayConfig::queue_imbalance_depth_levels)
      .def_readwrite("queue_imbalance_buy_threshold", &replay::ReplayConfig::queue_imbalance_buy_threshold)
      .def_readwrite("queue_imbalance_sell_threshold", &replay::ReplayConfig::queue_imbalance_sell_threshold)
      .def_readwrite("strategy_order_quantity", &replay::ReplayConfig::strategy_order_quantity)
      .def_readwrite("strategy_order_type", &replay::ReplayConfig::strategy_order_type)
      .def_property(
          "strategy_limit_price_ticks",
          [](const replay::ReplayConfig& config) { return optional_i64_to_object(config.strategy_limit_price_ticks); },
          [](replay::ReplayConfig& config, const py::object& value) {
            config.strategy_limit_price_ticks = object_to_optional_price_ticks(value);
          })
      .def_property(
          "order_latency_ns",
          [](const replay::ReplayConfig& config) { return config.order_latency.count; },
          [](replay::ReplayConfig& config, std::int64_t value) {
            config.order_latency = replay::LatencyNs::from_nanoseconds(value);
          })
      .def_property(
          "cancel_after_arrival_ns",
          [](const replay::ReplayConfig& config) { return optional_u64_to_object(config.cancel_after_arrival); },
          [](replay::ReplayConfig& config, const py::object& value) {
            config.cancel_after_arrival = object_to_optional_latency(value);
          })
      .def_readwrite("queue_fraction_ppm", &replay::ReplayConfig::queue_fraction_ppm)
      .def_readwrite("fee_rate_ppm", &replay::ReplayConfig::fee_rate_ppm)
      .def_readwrite("initial_cash", &replay::ReplayConfig::initial_cash)
      .def_property("scripted_intents", &get_scripted_intents, &set_scripted_intents);

  py::class_<PyOrderSummary>(m, "Order")
      .def_readonly("order_id", &PyOrderSummary::order_id)
      .def_readonly("side", &PyOrderSummary::side)
      .def_readonly("side_name", &PyOrderSummary::side_name)
      .def_readonly("order_type", &PyOrderSummary::order_type)
      .def_readonly("order_type_name", &PyOrderSummary::order_type_name)
      .def_readonly("original_quantity", &PyOrderSummary::original_quantity)
      .def_readonly("filled_quantity", &PyOrderSummary::filled_quantity)
      .def_readonly("remaining_quantity", &PyOrderSummary::remaining_quantity)
      .def_property_readonly("limit_price_ticks",
                             [](const PyOrderSummary& order) { return optional_i64_to_object(order.limit_price_ticks); })
      .def_readonly("decision_timestamp_ns", &PyOrderSummary::decision_timestamp_ns)
      .def_readonly("submit_timestamp_ns", &PyOrderSummary::submit_timestamp_ns)
      .def_readonly("exchange_arrival_timestamp_ns", &PyOrderSummary::exchange_arrival_timestamp_ns)
      .def_readonly("status", &PyOrderSummary::status)
      .def_readonly("status_name", &PyOrderSummary::status_name);

  py::class_<PyFillSummary>(m, "Fill")
      .def_readonly("fill_sequence_id", &PyFillSummary::fill_sequence_id)
      .def_readonly("order_id", &PyFillSummary::order_id)
      .def_readonly("timestamp_ns", &PyFillSummary::timestamp_ns)
      .def_readonly("side", &PyFillSummary::side)
      .def_readonly("side_name", &PyFillSummary::side_name)
      .def_readonly("price_ticks", &PyFillSummary::price_ticks)
      .def_readonly("quantity", &PyFillSummary::quantity)
      .def_readonly("fee", &PyFillSummary::fee);

  py::class_<PyReplayResult>(m, "ReplayResult")
      .def_readonly("processed_event_count", &PyReplayResult::processed_event_count)
      .def_readonly("book_update_count", &PyReplayResult::book_update_count)
      .def_readonly("trade_count", &PyReplayResult::trade_count)
      .def_readonly("internal_event_count", &PyReplayResult::internal_event_count)
      .def_readonly("strategy_intent_count", &PyReplayResult::strategy_intent_count)
      .def_readonly("order_count", &PyReplayResult::order_count)
      .def_readonly("orders_filled", &PyReplayResult::orders_filled)
      .def_readonly("orders_partially_filled", &PyReplayResult::orders_partially_filled)
      .def_readonly("orders_canceled", &PyReplayResult::orders_canceled)
      .def_readonly("orders_rejected", &PyReplayResult::orders_rejected)
      .def_readonly("fill_count", &PyReplayResult::fill_count)
      .def_readonly("start_timestamp_ns", &PyReplayResult::start_timestamp_ns)
      .def_readonly("end_timestamp_ns", &PyReplayResult::end_timestamp_ns)
      .def_readonly("initial_cash", &PyReplayResult::initial_cash)
      .def_readonly("ending_cash", &PyReplayResult::ending_cash)
      .def_readonly("ending_inventory", &PyReplayResult::ending_inventory)
      .def_readonly("realized_gross_pnl", &PyReplayResult::realized_gross_pnl)
      .def_readonly("total_fees", &PyReplayResult::total_fees)
      .def_readonly("turnover", &PyReplayResult::turnover)
      .def_property_readonly("final_mid_x2",
                             [](const PyReplayResult& result) { return optional_i64_to_object(result.final_mid_x2); })
      .def_property_readonly("unrealized_gross_pnl_x2", [](const PyReplayResult& result) {
        return optional_i64_to_object(result.unrealized_gross_pnl_x2);
      })
      .def_property_readonly("ending_equity_x2", [](const PyReplayResult& result) {
        return optional_i64_to_object(result.ending_equity_x2);
      })
      .def_property_readonly("net_total_pnl_x2", [](const PyReplayResult& result) {
        return optional_i64_to_object(result.net_total_pnl_x2);
      })
      .def_readonly("mark_available", &PyReplayResult::mark_available)
      .def_readonly("equity_determinable", &PyReplayResult::equity_determinable)
      .def_readonly("book_updates_hash", &PyReplayResult::book_updates_hash)
      .def_readonly("trades_hash", &PyReplayResult::trades_hash)
      .def_readonly("input_hash", &PyReplayResult::input_hash)
      .def_readonly("config_hash", &PyReplayResult::config_hash)
      .def_readonly("orders_hash", &PyReplayResult::orders_hash)
      .def_readonly("fills_hash", &PyReplayResult::fills_hash)
      .def_readonly("portfolio_hash", &PyReplayResult::portfolio_hash)
      .def_readonly("metrics_hash", &PyReplayResult::metrics_hash)
      .def_readonly("final_book_hash", &PyReplayResult::final_book_hash)
      .def_readonly("run_hash", &PyReplayResult::run_hash)
      .def_readonly("orders", &PyReplayResult::orders)
      .def_readonly("fills", &PyReplayResult::fills);

  py::class_<replay::ReplayEngine>(m, "ReplayEngine")
      .def(py::init<replay::ReplayConfig>(), py::arg("config"))
      .def("run", &run_engine);

  m.def("load_config", [](const std::string& path) { return replay::load_replay_config(path); }, py::arg("path"));
  m.def("canonical_config", &replay::canonical_replay_config, py::arg("config"));
  m.def("run_config_file",
        &run_config_file,
        py::arg("config_path"),
        py::arg("output_override") = py::none(),
        py::arg("force_output") = false);
}
