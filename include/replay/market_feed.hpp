#pragma once

#include "replay/types.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace replay {

enum class MarketEventType {
  BookUpdate,
  Trade,
};

enum class PriceFieldFormat {
  Decimal,
  Ticks,
};

struct FeedParserConfig {
  std::string tick_size{"0.01"};
  PriceFieldFormat price_format{PriceFieldFormat::Decimal};
};

class ParseError final : public std::runtime_error {
 public:
  ParseError(std::string source, std::size_t line_number, std::string field, std::string value, std::string reason);

  [[nodiscard]] const std::string& source() const noexcept;
  [[nodiscard]] std::size_t line_number() const noexcept;
  [[nodiscard]] const std::string& field() const noexcept;
  [[nodiscard]] const std::string& value() const noexcept;
  [[nodiscard]] const std::string& reason() const noexcept;

 private:
  std::string source_;
  std::size_t line_number_{};
  std::string field_;
  std::string value_;
  std::string reason_;
};

struct BookUpdateEvent {
  EventKey key{};
  Side side{};
  PriceTicks price_ticks{};
  Quantity quantity{};

  friend constexpr bool operator==(const BookUpdateEvent&, const BookUpdateEvent&) = default;
};

struct TradeEvent {
  EventKey key{};
  PriceTicks price_ticks{};
  Quantity quantity{};
  std::optional<Side> aggressor_side{};

  friend constexpr bool operator==(const TradeEvent&, const TradeEvent&) = default;
};

class MarketEvent {
 public:
  explicit MarketEvent(BookUpdateEvent book_update);
  explicit MarketEvent(TradeEvent trade);

  [[nodiscard]] MarketEventType type() const noexcept;
  [[nodiscard]] EventKey key() const noexcept;
  [[nodiscard]] const BookUpdateEvent& book_update() const;
  [[nodiscard]] const TradeEvent& trade() const;

  friend bool operator==(const MarketEvent& lhs, const MarketEvent& rhs);

 private:
  std::variant<BookUpdateEvent, TradeEvent> event_;
};

class NormalizedMarketFeed {
 public:
  NormalizedMarketFeed(std::vector<MarketEvent> events, std::string source);

  [[nodiscard]] const std::vector<MarketEvent>& events() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const std::string& source() const noexcept;

 private:
  std::vector<MarketEvent> events_;
  std::string source_;
};

NormalizedMarketFeed parse_book_updates_csv(std::istream& input,
                                            std::string_view source,
                                            const FeedParserConfig& config);
NormalizedMarketFeed parse_trades_csv(std::istream& input, std::string_view source, const FeedParserConfig& config);

NormalizedMarketFeed load_book_updates_csv(const std::filesystem::path& path, const FeedParserConfig& config);
NormalizedMarketFeed load_trades_csv(const std::filesystem::path& path, const FeedParserConfig& config);

std::string canonical_event_string(const MarketEvent& event);

}  // namespace replay
