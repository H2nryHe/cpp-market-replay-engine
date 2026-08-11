#include "replay/order_book.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace replay {
namespace {

constexpr std::uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t fnv1a_prime = 1099511628211ULL;

template <typename Levels>
std::vector<PriceLevel> top_levels(const Levels& levels, std::size_t max_levels) {
  std::vector<PriceLevel> result;
  result.reserve(std::min(max_levels, levels.size()));

  std::size_t emitted = 0;
  for (const auto& [price_ticks, quantity] : levels) {
    if (emitted == max_levels) {
      break;
    }
    result.push_back(PriceLevel{price_ticks, quantity});
    ++emitted;
  }

  return result;
}

template <typename Levels>
std::optional<Quantity> quantity_at(const Levels& levels, PriceTicks price_ticks) {
  validate_price_ticks(price_ticks);
  const auto found = levels.find(price_ticks);
  if (found == levels.end()) {
    return std::nullopt;
  }
  return found->second;
}

template <typename Levels>
bool has_zero_quantity(const Levels& levels) {
  for (const auto& [unused_price, quantity] : levels) {
    static_cast<void>(unused_price);
    if (quantity == 0) {
      return true;
    }
  }
  return false;
}

template <typename Levels>
bool has_negative_quantity(const Levels& levels) {
  for (const auto& [unused_price, quantity] : levels) {
    static_cast<void>(unused_price);
    if (quantity < 0) {
      return true;
    }
  }
  return false;
}

template <typename Levels>
void append_levels(std::ostringstream& os, char side_code, const Levels& levels) {
  for (const auto& [price_ticks, quantity] : levels) {
    os << side_code << ',' << price_ticks << ',' << quantity << '\n';
  }
}

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = fnv1a_offset_basis;
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= fnv1a_prime;
  }
  return hash;
}

}  // namespace

void OrderBook::apply(const BookUpdateEvent& update) {
  validate_price_ticks(update.price_ticks);
  validate_quantity(update.quantity);

  if (update.side == Side::Buy) {
    if (update.quantity == 0) {
      bids_.erase(update.price_ticks);
      return;
    }
    bids_[update.price_ticks] = update.quantity;
    return;
  }

  if (update.quantity == 0) {
    asks_.erase(update.price_ticks);
    return;
  }
  asks_[update.price_ticks] = update.quantity;
}

std::optional<PriceLevel> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  const auto& [price_ticks, quantity] = *bids_.begin();
  return PriceLevel{price_ticks, quantity};
}

std::optional<PriceLevel> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  const auto& [price_ticks, quantity] = *asks_.begin();
  return PriceLevel{price_ticks, quantity};
}

std::optional<Quantity> OrderBook::bid_quantity_at(PriceTicks price_ticks) const {
  return quantity_at(bids_, price_ticks);
}

std::optional<Quantity> OrderBook::ask_quantity_at(PriceTicks price_ticks) const {
  return quantity_at(asks_, price_ticks);
}

std::vector<PriceLevel> OrderBook::top_bids(std::size_t max_levels) const {
  return top_levels(bids_, max_levels);
}

std::vector<PriceLevel> OrderBook::top_asks(std::size_t max_levels) const {
  return top_levels(asks_, max_levels);
}

std::size_t OrderBook::bid_level_count() const noexcept {
  return bids_.size();
}

std::size_t OrderBook::ask_level_count() const noexcept {
  return asks_.size();
}

bool OrderBook::empty() const noexcept {
  return bids_.empty() && asks_.empty();
}

std::optional<std::int64_t> OrderBook::spread_ticks() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  if (!bid.has_value() || !ask.has_value()) {
    return std::nullopt;
  }
  return ask->price_ticks - bid->price_ticks;
}

std::optional<std::int64_t> OrderBook::mid_price_x2_ticks() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  if (!bid.has_value() || !ask.has_value()) {
    return std::nullopt;
  }

  if (bid->price_ticks > std::numeric_limits<std::int64_t>::max() - ask->price_ticks) {
    throw std::overflow_error("mid_price_x2_ticks overflow");
  }
  return bid->price_ticks + ask->price_ticks;
}

bool OrderBook::is_locked() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  return bid.has_value() && ask.has_value() && bid->price_ticks == ask->price_ticks;
}

bool OrderBook::is_crossed() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  return bid.has_value() && ask.has_value() && bid->price_ticks > ask->price_ticks;
}

bool OrderBook::is_valid_two_sided_market() const {
  const auto bid = best_bid();
  const auto ask = best_ask();
  return bid.has_value() && ask.has_value() && bid->price_ticks < ask->price_ticks;
}

bool OrderBook::has_zero_quantity_levels() const {
  return has_zero_quantity(bids_) || has_zero_quantity(asks_);
}

bool OrderBook::has_negative_quantity_levels() const {
  return has_negative_quantity(bids_) || has_negative_quantity(asks_);
}

bool OrderBook::bid_ordering_is_strict() const {
  if (bids_.size() < 2U) {
    return true;
  }

  auto previous = bids_.begin();
  auto current = std::next(previous);
  for (; current != bids_.end(); ++previous, ++current) {
    if (!(previous->first > current->first)) {
      return false;
    }
  }
  return true;
}

bool OrderBook::ask_ordering_is_strict() const {
  if (asks_.size() < 2U) {
    return true;
  }

  auto previous = asks_.begin();
  auto current = std::next(previous);
  for (; current != asks_.end(); ++previous, ++current) {
    if (!(previous->first < current->first)) {
      return false;
    }
  }
  return true;
}

std::string OrderBook::canonical_state() const {
  std::ostringstream os;
  append_levels(os, 'B', bids_);
  append_levels(os, 'A', asks_);
  return os.str();
}

std::uint64_t OrderBook::state_hash() const {
  return fnv1a64(canonical_state());
}

}  // namespace replay
