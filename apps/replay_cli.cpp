#include "replay/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_usage(std::ostream& os) {
  os << replay::engine_name() << " " << replay::engine_version() << '\n';
  os << "Usage: replay_cli --help\n\n";
  os << "Options:\n";
  os << "  --help    Show this help message and exit.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    print_usage(std::cout);
    return 0;
  }

  const std::string_view arg{argv[1]};
  if (arg == "--help" || arg == "-h") {
    print_usage(std::cout);
    return 0;
  }

  std::cerr << "error: unknown argument: " << arg << '\n';
  print_usage(std::cerr);
  return 1;
}
