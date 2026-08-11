#include "replay/version.hpp"

#include <iostream>
#include <string_view>

int main() {
  if (replay::engine_name() != std::string_view{"cpp-market-replay-engine"}) {
    std::cerr << "unexpected engine name\n";
    return 1;
  }

  if (replay::engine_version().empty()) {
    std::cerr << "engine version must not be empty\n";
    return 1;
  }

  return 0;
}
