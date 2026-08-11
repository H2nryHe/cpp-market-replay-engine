#pragma once

#include "replay/market_feed.hpp"
#include "replay/types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace replay {

struct PriceLevel {
  PriceTicks price_ticks{};
  Quantity quantity{};

  friend constexpr bool operator==(const PriceLevel&, const PriceLevel&) = default;
};

class OrderBook {
 public:
  void apply(const BookUpdateEvent& update);

  [[nodiscard]] std::optional<PriceLevel> best_bid() const;
  [[nodiscard]] std::optional<PriceLevel> best_ask() const;

  [[nodiscard]] std::optional<Quantity> bid_quantity_at(PriceTicks price_ticks) const;
  [[nodiscard]] std::optional<Quantity> ask_quantity_at(PriceTicks price_ticks) const;

  [[nodiscard]] std::vector<PriceLevel> top_bids(std::size_t max_levels) const;
  [[nodiscard]] std::vector<PriceLevel> top_asks(std::size_t max_levels) const;

  [[nodiscard]] std::size_t bid_level_count() const noexcept;
  [[nodiscard]] std::size_t ask_level_count() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  [[nodiscard]] std::optional<std::int64_t> spread_ticks() const;
  [[nodiscard]] std::optional<std::int64_t> mid_price_x2_ticks() const;

  [[nodiscard]] bool is_locked() const;
  [[nodiscard]] bool is_crossed() const;
  [[nodiscard]] bool is_valid_two_sided_market() const;

  [[nodiscard]] bool has_zero_quantity_levels() const;
  [[nodiscard]] bool has_negative_quantity_levels() const;
  [[nodiscard]] bool bid_ordering_is_strict() const;
  [[nodiscard]] bool ask_ordering_is_strict() const;

  [[nodiscard]] std::string canonical_state() const;
  [[nodiscard]] std::uint64_t state_hash() const;

 private:
  using BidLevels = std::map<PriceTicks, Quantity, std::greater<PriceTicks>>;
  using AskLevels = std::map<PriceTicks, Quantity, std::less<PriceTicks>>;

  BidLevels bids_;
  AskLevels asks_;
};

}  // namespace replay
