#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace replay {

using TimestampNs = std::uint64_t;
using PriceTicks = std::int64_t;
using Quantity = std::int64_t;

enum class Side {
  Buy,
  Sell,
};

enum class OrderType {
  Market,
  Limit,
};

enum class OrderStatus {
  New,
  Pending,
  Acknowledged,
  PartiallyFilled,
  Filled,
  Canceled,
  Rejected,
};

struct EventKey {
  TimestampNs timestamp_ns{};
  std::uint64_t sequence_id{};

  friend constexpr bool operator==(const EventKey&, const EventKey&) = default;
  friend constexpr bool operator<(const EventKey& lhs, const EventKey& rhs) noexcept {
    if (lhs.timestamp_ns != rhs.timestamp_ns) {
      return lhs.timestamp_ns < rhs.timestamp_ns;
    }
    return lhs.sequence_id < rhs.sequence_id;
  }
};

struct LatencyNs {
  TimestampNs count{};

  static LatencyNs from_nanoseconds(std::int64_t latency_ns);
  static LatencyNs from_microseconds(std::int64_t latency_us);
};

Side parse_side(std::string_view text);
OrderType parse_order_type(std::string_view text);
OrderStatus parse_order_status(std::string_view text);

PriceTicks price_to_ticks(std::string_view decimal_price, std::string_view tick_size);
std::string ticks_to_price(PriceTicks price_ticks, std::string_view tick_size);

void validate_price_ticks(PriceTicks price_ticks);
void validate_quantity(Quantity quantity);
void validate_tick_size(std::string_view tick_size);

}  // namespace replay
