#include "replay/version.hpp"

namespace replay {

#ifndef REPLAY_ENGINE_VERSION
#define REPLAY_ENGINE_VERSION "0.1.0"
#endif

std::string_view engine_name() noexcept {
  return "cpp-market-replay-engine";
}

std::string_view engine_version() noexcept {
  return REPLAY_ENGINE_VERSION;
}

}  // namespace replay
