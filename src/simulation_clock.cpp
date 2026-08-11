#include "replay/simulation_clock.hpp"

#include <stdexcept>

namespace replay {

SimulationClock::SimulationClock(TimestampNs initial_time_ns) : current_time_ns_{initial_time_ns} {}

TimestampNs SimulationClock::now() const noexcept {
  return current_time_ns_;
}

void SimulationClock::advance_to(TimestampNs timestamp_ns) {
  if (timestamp_ns < current_time_ns_) {
    throw std::invalid_argument("simulation clock cannot move backward");
  }
  current_time_ns_ = timestamp_ns;
}

}  // namespace replay
