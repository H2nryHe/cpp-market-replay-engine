#include "replay/version.hpp"

namespace replay {

std::string_view engine_name() noexcept {
  return "cpp-market-replay-engine";
}

std::string_view engine_version() noexcept {
  return "0.1.0";
}

}  // namespace replay
