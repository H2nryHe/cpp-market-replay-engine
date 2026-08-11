#include "replay/types.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace replay {
namespace {

struct Decimal {
  std::uint64_t coefficient{};
  std::uint32_t scale{};
};

bool is_digit(char value) noexcept {
  return value >= '0' && value <= '9';
}

std::uint64_t checked_mul10_add(std::uint64_t value, std::uint64_t digit, std::string_view field_name) {
  constexpr auto max_value = std::numeric_limits<std::uint64_t>::max();
  if (value > (max_value - digit) / 10U) {
    throw std::out_of_range(std::string{field_name} + " is too large");
  }
  return (value * 10U) + digit;
}

Decimal parse_non_negative_decimal(std::string_view text, std::string_view field_name) {
  if (text.empty()) {
    throw std::invalid_argument(std::string{field_name} + " must not be empty");
  }

  std::size_t index = 0;
  if (text[index] == '+') {
    ++index;
    if (index == text.size()) {
      throw std::invalid_argument(std::string{field_name} + " must contain digits");
    }
  } else if (text[index] == '-') {
    throw std::invalid_argument(std::string{field_name} + " must be non-negative");
  }

  std::uint64_t coefficient = 0;
  std::uint32_t scale = 0;
  bool saw_digit = false;
  bool saw_decimal_point = false;

  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (is_digit(ch)) {
      saw_digit = true;
      coefficient = checked_mul10_add(coefficient, static_cast<std::uint64_t>(ch - '0'), field_name);
      if (saw_decimal_point) {
        if (scale == std::numeric_limits<std::uint32_t>::max()) {
          throw std::out_of_range(std::string{field_name} + " has too many decimal places");
        }
        ++scale;
      }
      continue;
    }

    if (ch == '.' && !saw_decimal_point) {
      saw_decimal_point = true;
      continue;
    }

    throw std::invalid_argument(std::string{field_name} + " must be a plain decimal number");
  }

  if (!saw_digit) {
    throw std::invalid_argument(std::string{field_name} + " must contain digits");
  }

  while (scale > 0 && (coefficient % 10U) == 0U) {
    coefficient /= 10U;
    --scale;
  }

  return Decimal{coefficient, scale};
}

std::uint64_t pow10(std::uint32_t exponent) {
  std::uint64_t value = 1;
  for (std::uint32_t count = 0; count < exponent; ++count) {
    if (value > std::numeric_limits<std::uint64_t>::max() / 10U) {
      throw std::out_of_range("decimal scale is too large");
    }
    value *= 10U;
  }
  return value;
}

std::uint64_t checked_to_uint64(unsigned __int128 value, std::string_view field_name) {
  if (value > std::numeric_limits<std::uint64_t>::max()) {
    throw std::out_of_range(std::string{field_name} + " is too large");
  }
  return static_cast<std::uint64_t>(value);
}

std::string format_scaled_decimal(std::uint64_t coefficient, std::uint32_t scale) {
  std::string digits = std::to_string(coefficient);
  if (scale == 0) {
    return digits;
  }

  const auto scale_size = static_cast<std::size_t>(scale);
  if (digits.size() <= scale_size) {
    digits.insert(digits.begin(), scale_size - digits.size() + 1U, '0');
  }

  const auto decimal_position = digits.size() - scale_size;
  digits.insert(decimal_position, 1U, '.');
  return digits;
}

Decimal parse_tick_size(std::string_view tick_size) {
  auto parsed = parse_non_negative_decimal(tick_size, "tick_size");
  if (parsed.coefficient == 0U) {
    throw std::invalid_argument("tick_size must be greater than zero");
  }
  return parsed;
}

}  // namespace

LatencyNs LatencyNs::from_nanoseconds(std::int64_t latency_ns) {
  if (latency_ns < 0) {
    throw std::invalid_argument("latency_ns must be non-negative");
  }
  return LatencyNs{static_cast<TimestampNs>(latency_ns)};
}

LatencyNs LatencyNs::from_microseconds(std::int64_t latency_us) {
  if (latency_us < 0) {
    throw std::invalid_argument("latency_us must be non-negative");
  }
  if (latency_us > std::numeric_limits<std::int64_t>::max() / 1000) {
    throw std::out_of_range("latency_us is too large");
  }
  return from_nanoseconds(latency_us * 1000);
}

Side parse_side(std::string_view text) {
  if (text == "buy" || text == "B") {
    return Side::Buy;
  }
  if (text == "sell" || text == "S") {
    return Side::Sell;
  }
  throw std::invalid_argument("invalid side: expected buy, sell, B, or S");
}

OrderType parse_order_type(std::string_view text) {
  if (text == "market") {
    return OrderType::Market;
  }
  if (text == "limit") {
    return OrderType::Limit;
  }
  throw std::invalid_argument("invalid order type: expected market or limit");
}

OrderStatus parse_order_status(std::string_view text) {
  if (text == "new") {
    return OrderStatus::New;
  }
  if (text == "pending") {
    return OrderStatus::Pending;
  }
  if (text == "acknowledged") {
    return OrderStatus::Acknowledged;
  }
  if (text == "partially_filled") {
    return OrderStatus::PartiallyFilled;
  }
  if (text == "filled") {
    return OrderStatus::Filled;
  }
  if (text == "canceled") {
    return OrderStatus::Canceled;
  }
  if (text == "rejected") {
    return OrderStatus::Rejected;
  }
  throw std::invalid_argument("invalid order status");
}

PriceTicks price_to_ticks(std::string_view decimal_price, std::string_view tick_size) {
  const auto price = parse_non_negative_decimal(decimal_price, "decimal_price");
  const auto tick = parse_tick_size(tick_size);

  const auto numerator_scale = pow10(tick.scale);
  const auto denominator_scale = pow10(price.scale);
  const auto numerator =
      static_cast<unsigned __int128>(price.coefficient) * static_cast<unsigned __int128>(numerator_scale);
  const auto denominator =
      static_cast<unsigned __int128>(tick.coefficient) * static_cast<unsigned __int128>(denominator_scale);

  if (denominator == 0U) {
    throw std::invalid_argument("tick_size must be greater than zero");
  }
  if ((numerator % denominator) != 0U) {
    throw std::invalid_argument("decimal_price is not an exact multiple of tick_size");
  }

  const auto ticks = checked_to_uint64(numerator / denominator, "price_ticks");
  if (ticks > static_cast<std::uint64_t>(std::numeric_limits<PriceTicks>::max())) {
    throw std::out_of_range("price_ticks exceeds PriceTicks range");
  }
  return static_cast<PriceTicks>(ticks);
}

std::string ticks_to_price(PriceTicks price_ticks, std::string_view tick_size) {
  validate_price_ticks(price_ticks);
  const auto tick = parse_tick_size(tick_size);
  const auto unsigned_ticks = static_cast<std::uint64_t>(price_ticks);
  const auto scaled_price =
      static_cast<unsigned __int128>(unsigned_ticks) * static_cast<unsigned __int128>(tick.coefficient);
  const auto coefficient = checked_to_uint64(scaled_price, "decimal_price");
  return format_scaled_decimal(coefficient, tick.scale);
}

void validate_price_ticks(PriceTicks price_ticks) {
  if (price_ticks < 0) {
    throw std::invalid_argument("price_ticks must be non-negative");
  }
}

void validate_quantity(Quantity quantity) {
  if (quantity < 0) {
    throw std::invalid_argument("quantity must be non-negative");
  }
}

void validate_tick_size(std::string_view tick_size) {
  static_cast<void>(parse_tick_size(tick_size));
}

}  // namespace replay
