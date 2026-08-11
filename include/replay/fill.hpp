#pragma once

#include "replay/types.hpp"

#include <string>

namespace replay {

struct Fill {
  OrderId order_id{};
  Side side{};
  PriceTicks price_ticks{};
  Quantity quantity{};
  TimestampNs fill_timestamp_ns{};
  FillSequenceId fill_sequence_id{};
  FeeAmount fee_amount{};

  friend constexpr bool operator==(const Fill&, const Fill&) = default;
};

std::string canonical_fill_string(const Fill& fill);

}  // namespace replay
