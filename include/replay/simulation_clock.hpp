#pragma once

#include "replay/types.hpp"

namespace replay {

class SimulationClock {
 public:
  SimulationClock() = default;
  explicit SimulationClock(TimestampNs initial_time_ns);

  [[nodiscard]] TimestampNs now() const noexcept;
  void advance_to(TimestampNs timestamp_ns);

 private:
  TimestampNs current_time_ns_{0};
};

}  // namespace replay
