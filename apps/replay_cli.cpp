#include "replay/replay_engine.hpp"
#include "replay/version.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& os) {
  os << replay::engine_name() << " " << replay::engine_version() << '\n';
  os << "Usage:\n";
  os << "  replay_cli --config <path> [--output <directory>] [--force]\n";
  os << "  replay_cli --help\n\n";
  os << "Options:\n";
  os << "  --config <path>       Key=value replay configuration file.\n";
  os << "  --output <directory>  Artifact output directory; overrides config output_directory.\n";
  os << "  --force               Replace an existing artifact directory.\n";
  os << "  --help, -h            Show this help message and exit.\n";
}

std::string display_path(const std::filesystem::path& path) {
  if (path.is_absolute()) {
    return "<absolute-output-dir>";
  }
  return path.generic_string();
}

void print_summary(const replay::ReplayRunResult& result, const std::filesystem::path& output_directory) {
  const bool mark_available = result.final_mark.has_value() && result.final_mark->mid_price_x2_ticks.has_value();
  const bool equity_available = result.final_mark.has_value();

  std::cout << replay::engine_name() << "\n\n";
  std::cout << "Events processed:      " << result.metrics.market_events_processed << '\n';
  std::cout << "Book updates:          " << result.metrics.book_updates_processed << '\n';
  std::cout << "Trades:                " << result.metrics.trades_processed << '\n';
  std::cout << "Orders submitted:      " << result.metrics.orders_submitted << '\n';
  std::cout << "Fills:                 " << result.metrics.fills << '\n';
  std::cout << "Ending inventory:      " << result.portfolio.inventory() << '\n';
  std::cout << "Realized gross PnL:    " << result.portfolio.realized_gross_pnl() << '\n';
  std::cout << "Unrealized gross PnL:  ";
  if (mark_available) {
    std::cout << result.final_mark->unrealized_gross_pnl_x2 << " x2\n";
  } else {
    std::cout << "unavailable\n";
  }
  std::cout << "Fees:                  " << result.portfolio.total_fees() << '\n';
  std::cout << "Ending equity:         ";
  if (equity_available) {
    std::cout << result.final_mark->equity_x2 << " x2\n";
  } else {
    std::cout << "unavailable\n";
  }
  std::cout << "Final book hash:       " << result.hashes.final_book_hash << '\n';
  std::cout << "Run hash:              " << result.hashes.run_hash << '\n';
  std::cout << "Artifacts:             " << display_path(output_directory) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    print_usage(std::cout);
    return 0;
  }

  std::optional<std::filesystem::path> config_path;
  std::optional<std::filesystem::path> output_directory;
  bool force = false;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--force") {
      force = true;
      continue;
    }
    if (arg == "--config") {
      if (index + 1 >= argc) {
        std::cerr << "error: --config requires a path\n";
        return 1;
      }
      config_path = argv[++index];
      continue;
    }
    if (arg == "--output") {
      if (index + 1 >= argc) {
        std::cerr << "error: --output requires a directory\n";
        return 1;
      }
      output_directory = argv[++index];
      continue;
    }

    std::cerr << "error: unknown argument: " << arg << '\n';
    print_usage(std::cerr);
    return 1;
  }

  if (!config_path.has_value()) {
    std::cerr << "error: --config is required\n";
    print_usage(std::cerr);
    return 1;
  }

  try {
    auto result = replay::run_replay_from_config_file(*config_path, output_directory, force);
    print_summary(result, output_directory.value_or(result.config.output_directory));
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
