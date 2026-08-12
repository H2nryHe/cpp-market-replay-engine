#include "replay/event_loop.hpp"
#include "replay/market_feed.hpp"
#include "replay/order_book.hpp"
#include "replay/replay_engine.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkOptions {
  std::vector<std::size_t> scales{100'000, 1'000'000};
  std::size_t repetitions{5};
  std::size_t warmups{1};
  std::filesystem::path output_dir{"benchmarks/results"};
  bool include_e2e{true};
  bool self_test{false};
};

struct BenchmarkRow {
  std::string benchmark_name{};
  std::size_t event_count{};
  std::size_t repetition{};
  std::uint64_t elapsed_ns{};
  double events_per_sec{};
  double ns_per_event{};
  std::string final_state_hash{};
};

struct SummaryRow {
  std::string benchmark_name{};
  std::size_t event_count{};
  std::size_t repetitions{};
  std::size_t warmups{};
  double median_events_per_sec{};
  double median_ns_per_event{};
  double min_ns_per_event{};
  double max_ns_per_event{};
  std::string final_state_hash{};
};

std::string hex64(std::uint64_t value) {
  std::ostringstream os;
  os << std::hex << std::setfill('0') << std::setw(16) << value;
  return os.str();
}

void checksum_mix(std::uint64_t& checksum, std::uint64_t value) noexcept {
  checksum ^= value + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
}

std::uint64_t side_checksum_value(replay::Side side) {
  switch (side) {
    case replay::Side::Buy:
      return 1;
    case replay::Side::Sell:
      return 2;
  }
  throw std::invalid_argument("invalid Side");
}

std::uint64_t event_iteration_checksum(const std::vector<replay::MarketEvent>& events) {
  std::uint64_t checksum = 0xcbf29ce484222325ULL;
  const replay::MarketEvent* volatile observed_event = nullptr;
  std::size_t index = 0;
  for (const auto& event : events) {
    observed_event = &event;
    checksum_mix(checksum, static_cast<std::uint64_t>(index));
    ++index;
  }
  static_cast<void>(observed_event);
  return checksum;
}

std::uint64_t event_dispatch_checksum(const std::vector<replay::MarketEvent>& events) {
  std::uint64_t checksum = 0x84222325cbf29ce4ULL;
  for (const auto& event : events) {
    const auto key = event.key();
    checksum_mix(checksum, key.timestamp_ns);
    checksum_mix(checksum, key.sequence_id);
    switch (event.type()) {
      case replay::MarketEventType::BookUpdate: {
        const auto& update = event.book_update();
        checksum_mix(checksum, 1);
        checksum_mix(checksum, side_checksum_value(update.side));
        checksum_mix(checksum, static_cast<std::uint64_t>(update.price_ticks));
        checksum_mix(checksum, static_cast<std::uint64_t>(update.quantity));
        break;
      }
      case replay::MarketEventType::Trade: {
        const auto& trade = event.trade();
        checksum_mix(checksum, 2);
        checksum_mix(checksum, static_cast<std::uint64_t>(trade.price_ticks));
        checksum_mix(checksum, static_cast<std::uint64_t>(trade.quantity));
        checksum_mix(checksum, trade.aggressor_side ? side_checksum_value(*trade.aggressor_side) : 0);
        break;
      }
    }
  }
  return checksum;
}

std::uint64_t elapsed_ns(Clock::time_point begin, Clock::time_point end) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

double events_per_second(std::size_t event_count, std::uint64_t elapsed) {
  return (static_cast<double>(event_count) * 1'000'000'000.0) / static_cast<double>(elapsed);
}

double ns_per_event(std::size_t event_count, std::uint64_t elapsed) {
  return static_cast<double>(elapsed) / static_cast<double>(event_count);
}

std::vector<std::string> split(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const auto pos = text.find(delimiter, begin);
    const auto end = pos == std::string_view::npos ? text.size() : pos;
    parts.emplace_back(text.substr(begin, end - begin));
    if (pos == std::string_view::npos) {
      break;
    }
    begin = pos + 1U;
  }
  return parts;
}

std::size_t parse_size(std::string_view text, std::string_view field) {
  std::size_t value = 0;
  const auto* const begin = text.data();
  const auto* const end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw std::invalid_argument(std::string{field} + " must be a non-negative integer");
  }
  return value;
}

BenchmarkOptions parse_options(int argc, char** argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--self-test") {
      options.self_test = true;
      options.scales = {1'000, 5'000};
      options.repetitions = 2;
      options.warmups = 1;
      options.include_e2e = false;
      continue;
    }
    if (arg == "--output-dir") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--output-dir requires a path");
      }
      options.output_dir = argv[++index];
      continue;
    }
    if (arg == "--repetitions") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--repetitions requires a value");
      }
      options.repetitions = parse_size(argv[++index], "repetitions");
      continue;
    }
    if (arg == "--warmups") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--warmups requires a value");
      }
      options.warmups = parse_size(argv[++index], "warmups");
      continue;
    }
    if (arg == "--scales") {
      if (index + 1 >= argc) {
        throw std::invalid_argument("--scales requires comma-separated values");
      }
      options.scales.clear();
      for (const auto& part : split(argv[++index], ',')) {
        options.scales.push_back(parse_size(part, "scale"));
      }
      continue;
    }
    if (arg == "--no-e2e") {
      options.include_e2e = false;
      continue;
    }
    if (arg == "--help") {
      std::cout << "Usage: benchmark_replay [--output-dir path] [--scales 100000,1000000]"
                   " [--repetitions N] [--warmups N] [--no-e2e] [--self-test]\n";
      std::exit(EXIT_SUCCESS);
    }
    throw std::invalid_argument("unknown benchmark argument: " + std::string{arg});
  }
  if (options.scales.empty()) {
    throw std::invalid_argument("at least one benchmark scale is required");
  }
  if (options.repetitions == 0) {
    throw std::invalid_argument("repetitions must be greater than zero");
  }
  return options;
}

replay::BookUpdateEvent generated_update(std::size_t index) {
  const bool buy_side = (index % 2U) == 0U;
  const auto level = static_cast<replay::PriceTicks>((index * (buy_side ? 17U : 19U)) % 256U);
  const auto price_ticks = buy_side ? static_cast<replay::PriceTicks>(9'800 + level)
                                    : static_cast<replay::PriceTicks>(10'200 + level);
  replay::Quantity quantity = static_cast<replay::Quantity>(((index * 31U) % 100U) + 1U);
  if ((index % 23U) == 0U) {
    quantity = 0;
  }
  return replay::BookUpdateEvent{
      .key = replay::EventKey{static_cast<replay::TimestampNs>(index), static_cast<std::uint64_t>(index)},
      .side = buy_side ? replay::Side::Buy : replay::Side::Sell,
      .price_ticks = price_ticks,
      .quantity = quantity,
  };
}

std::vector<replay::BookUpdateEvent> generate_book_updates(std::size_t event_count) {
  std::vector<replay::BookUpdateEvent> updates;
  updates.reserve(event_count);
  for (std::size_t index = 0; index < event_count; ++index) {
    updates.push_back(generated_update(index));
  }
  return updates;
}

std::vector<replay::MarketEvent> generate_market_events(std::size_t event_count) {
  std::vector<replay::MarketEvent> events;
  events.reserve(event_count);
  for (std::size_t index = 0; index < event_count; ++index) {
    if ((index % 5U) == 4U) {
      events.emplace_back(replay::TradeEvent{
          .key = replay::EventKey{static_cast<replay::TimestampNs>(index), static_cast<std::uint64_t>(index)},
          .price_ticks = static_cast<replay::PriceTicks>(10'000 + (index % 16U)),
          .quantity = static_cast<replay::Quantity>((index % 10U) + 1U),
          .aggressor_side = (index % 2U) == 0U ? replay::Side::Buy : replay::Side::Sell,
      });
      continue;
    }
    events.emplace_back(generated_update(index));
  }
  return events;
}

std::string book_hash_for_updates(const std::vector<replay::BookUpdateEvent>& updates) {
  replay::OrderBook book;
  for (const auto& update : updates) {
    book.apply(update);
  }
  return hex64(book.state_hash());
}

std::string book_hash_for_events(const std::vector<replay::MarketEvent>& events) {
  replay::OrderBook book;
  replay::EventLoop loop{events};
  static_cast<void>(loop.run(replay::EventLoopHandlers{.order_book = &book}));
  return hex64(book.state_hash());
}

BenchmarkRow make_row(std::string benchmark_name,
                      std::size_t event_count,
                      std::size_t repetition,
                      std::uint64_t elapsed,
                      std::string final_hash) {
  return BenchmarkRow{std::move(benchmark_name),
                      event_count,
                      repetition,
                      elapsed,
                      events_per_second(event_count, elapsed),
                      ns_per_event(event_count, elapsed),
                      std::move(final_hash)};
}

BenchmarkRow run_order_book_once(const std::vector<replay::BookUpdateEvent>& updates, std::size_t repetition) {
  replay::OrderBook book;
  const auto begin = Clock::now();
  for (const auto& update : updates) {
    book.apply(update);
  }
  const auto end = Clock::now();
  return make_row("order_book_apply", updates.size(), repetition, elapsed_ns(begin, end), hex64(book.state_hash()));
}

BenchmarkRow run_bare_event_iteration_once(const std::vector<replay::MarketEvent>& events, std::size_t repetition) {
  const auto begin = Clock::now();
  const auto checksum = event_iteration_checksum(events);
  const auto end = Clock::now();
  return make_row("bare_event_iteration", events.size(), repetition, elapsed_ns(begin, end), hex64(checksum));
}

BenchmarkRow run_event_dispatch_once(const std::vector<replay::MarketEvent>& events, std::size_t repetition) {
  const auto begin = Clock::now();
  const auto checksum = event_dispatch_checksum(events);
  const auto end = Clock::now();
  return make_row("event_type_dispatch_only", events.size(), repetition, elapsed_ns(begin, end), hex64(checksum));
}

BenchmarkRow run_core_replay_once(const std::vector<replay::MarketEvent>& events, std::size_t repetition) {
  auto run_events = events;
  replay::OrderBook book;
  const auto begin = Clock::now();
  replay::EventLoop loop{std::move(run_events)};
  const auto result = loop.run(replay::EventLoopHandlers{.order_book = &book});
  const auto end = Clock::now();
  if (result.processed_event_count != events.size()) {
    throw std::runtime_error("core replay processed event count mismatch");
  }
  return make_row("core_preloaded_replay", events.size(), repetition, elapsed_ns(begin, end), hex64(book.state_hash()));
}

BenchmarkRow run_e2e_once(std::size_t repetition, const std::filesystem::path& output_dir) {
  const auto run_dir = output_dir / ("e2e_repetition_" + std::to_string(repetition));
  const auto begin = Clock::now();
  const auto result = replay::run_replay_from_config_file("configs/example_config.kv", run_dir, true);
  const auto end = Clock::now();
  return make_row("end_to_end_public_example",
                  result.metrics.market_events_processed,
                  repetition,
                  elapsed_ns(begin, end),
                  result.hashes.run_hash);
}

template <typename Runner>
void run_repeated(std::vector<BenchmarkRow>& rows,
                  std::size_t warmups,
                  std::size_t repetitions,
                  Runner runner) {
  for (std::size_t warmup = 0; warmup < warmups; ++warmup) {
    static_cast<void>(runner(static_cast<std::size_t>(0)));
  }
  std::string expected_hash;
  for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
    auto row = runner(repetition);
    if (repetition == 0) {
      expected_hash = row.final_state_hash;
    } else if (row.final_state_hash != expected_hash) {
      throw std::runtime_error("benchmark final hash changed between repetitions");
    }
    rows.push_back(std::move(row));
  }
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2U;
  if ((values.size() % 2U) == 1U) {
    return values[middle];
  }
  return (values[middle - 1U] + values[middle]) / 2.0;
}

std::vector<SummaryRow> summarize(const std::vector<BenchmarkRow>& rows, std::size_t repetitions, std::size_t warmups) {
  std::vector<SummaryRow> summaries;
  for (const auto& row : rows) {
    const bool already_seen = std::any_of(summaries.begin(), summaries.end(), [&row](const SummaryRow& summary) {
      return summary.benchmark_name == row.benchmark_name && summary.event_count == row.event_count;
    });
    if (already_seen) {
      continue;
    }

    std::vector<double> eps;
    std::vector<double> npe;
    std::string expected_hash;
    for (const auto& candidate : rows) {
      if (candidate.benchmark_name != row.benchmark_name || candidate.event_count != row.event_count) {
        continue;
      }
      eps.push_back(candidate.events_per_sec);
      npe.push_back(candidate.ns_per_event);
      if (expected_hash.empty()) {
        expected_hash = candidate.final_state_hash;
      } else if (expected_hash != candidate.final_state_hash) {
        throw std::runtime_error("summary detected non-deterministic final hash");
      }
    }
    summaries.push_back(SummaryRow{row.benchmark_name,
                                   row.event_count,
                                   repetitions,
                                   warmups,
                                   median(eps),
                                   median(npe),
                                   *std::min_element(npe.begin(), npe.end()),
                                   *std::max_element(npe.begin(), npe.end()),
                                   expected_hash});
  }
  return summaries;
}

void write_repetitions_csv(const std::filesystem::path& path, const std::vector<BenchmarkRow>& rows) {
  std::ofstream output{path};
  output << "benchmark_name,event_count,repetition,elapsed_ns,events_per_sec,ns_per_event,final_state_hash\n";
  output << std::fixed << std::setprecision(3);
  for (const auto& row : rows) {
    output << row.benchmark_name << ',' << row.event_count << ',' << row.repetition << ',' << row.elapsed_ns << ','
           << row.events_per_sec << ',' << row.ns_per_event << ',' << row.final_state_hash << '\n';
  }
}

void write_summary_csv(const std::filesystem::path& path, const std::vector<SummaryRow>& rows) {
  std::ofstream output{path};
  output << "benchmark_name,event_count,repetitions,warmups,median_events_per_sec,median_ns_per_event,"
            "min_ns_per_event,max_ns_per_event,final_state_hash\n";
  output << std::fixed << std::setprecision(3);
  for (const auto& row : rows) {
    output << row.benchmark_name << ',' << row.event_count << ',' << row.repetitions << ',' << row.warmups << ','
           << row.median_events_per_sec << ',' << row.median_ns_per_event << ',' << row.min_ns_per_event << ','
           << row.max_ns_per_event << ',' << row.final_state_hash << '\n';
  }
}

void run_self_test() {
  const auto updates_a = generate_book_updates(1'000);
  const auto updates_b = generate_book_updates(1'000);
  if (book_hash_for_updates(updates_a) != book_hash_for_updates(updates_b)) {
    throw std::runtime_error("book update generator is non-deterministic");
  }
  const auto events_a = generate_market_events(5'000);
  const auto events_b = generate_market_events(5'000);
  if (book_hash_for_events(events_a) != book_hash_for_events(events_b)) {
    throw std::runtime_error("market event generator is non-deterministic");
  }
  if (event_iteration_checksum(events_a) != event_iteration_checksum(events_b)) {
    throw std::runtime_error("bare event checksum is non-deterministic");
  }
  if (event_dispatch_checksum(events_a) != event_dispatch_checksum(events_b)) {
    throw std::runtime_error("event dispatch checksum is non-deterministic");
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    if (options.self_test) {
      run_self_test();
      return EXIT_SUCCESS;
    }

    std::filesystem::create_directories(options.output_dir);
    std::vector<BenchmarkRow> rows;

    for (const auto scale : options.scales) {
      const auto updates = generate_book_updates(scale);
      const auto expected_update_hash = book_hash_for_updates(updates);
      run_repeated(rows, options.warmups, options.repetitions, [&updates, &expected_update_hash](std::size_t repetition) {
        auto row = run_order_book_once(updates, repetition);
        if (row.final_state_hash != expected_update_hash) {
          throw std::runtime_error("order book benchmark hash mismatch");
        }
        return row;
      });

      const auto events = generate_market_events(scale);
      const auto expected_iteration_checksum = hex64(event_iteration_checksum(events));
      run_repeated(rows,
                   options.warmups,
                   options.repetitions,
                   [&events, &expected_iteration_checksum](std::size_t repetition) {
                     auto row = run_bare_event_iteration_once(events, repetition);
                     if (row.final_state_hash != expected_iteration_checksum) {
                       throw std::runtime_error("bare event iteration checksum mismatch");
                     }
                     return row;
                   });

      const auto expected_dispatch_checksum = hex64(event_dispatch_checksum(events));
      run_repeated(rows,
                   options.warmups,
                   options.repetitions,
                   [&events, &expected_dispatch_checksum](std::size_t repetition) {
                     auto row = run_event_dispatch_once(events, repetition);
                     if (row.final_state_hash != expected_dispatch_checksum) {
                       throw std::runtime_error("event dispatch checksum mismatch");
                     }
                     return row;
                   });

      const auto expected_event_hash = book_hash_for_events(events);
      run_repeated(rows, options.warmups, options.repetitions, [&events, &expected_event_hash](std::size_t repetition) {
        auto row = run_core_replay_once(events, repetition);
        if (row.final_state_hash != expected_event_hash) {
          throw std::runtime_error("core replay benchmark hash mismatch");
        }
        return row;
      });
    }

    if (options.include_e2e) {
      run_repeated(rows, options.warmups, options.repetitions, [&options](std::size_t repetition) {
        auto row = run_e2e_once(repetition, options.output_dir);
        if (row.final_state_hash != "8aca37583ca6f83a") {
          throw std::runtime_error("end-to-end golden run hash mismatch");
        }
        return row;
      });
    }

    const auto summaries = summarize(rows, options.repetitions, options.warmups);
    write_repetitions_csv(options.output_dir / "baseline_repetitions.csv", rows);
    write_summary_csv(options.output_dir / "baseline_summary.csv", summaries);
    for (const auto& summary : summaries) {
      std::cout << summary.benchmark_name << " events=" << summary.event_count
                << " median_events_sec=" << std::fixed << std::setprecision(3) << summary.median_events_per_sec
                << " median_ns_event=" << summary.median_ns_per_event << " hash=" << summary.final_state_hash << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "benchmark error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
