#include "replay/market_feed.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

namespace replay {
namespace {

constexpr std::size_t timestamp_column = 0;
constexpr std::size_t sequence_column = 1;

std::string make_parse_error_message(std::string_view source,
                                     std::size_t line_number,
                                     std::string_view field,
                                     std::string_view value,
                                     std::string_view reason) {
  std::ostringstream os;
  os << source << ':' << line_number << ": field '" << field << "' value '" << value << "': " << reason;
  return os.str();
}

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\t' || text[end - 1U] == '\r')) {
    --end;
  }

  return std::string{text.substr(begin, end - begin)};
}

std::vector<std::string> split_csv_line(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const auto comma = line.find(',', begin);
    const auto end = comma == std::string_view::npos ? line.size() : comma;
    fields.push_back(trim(line.substr(begin, end - begin)));
    if (comma == std::string_view::npos) {
      break;
    }
    begin = comma + 1U;
  }
  return fields;
}

bool is_blank(std::string_view line) {
  for (const char ch : line) {
    if (ch != ' ' && ch != '\t' && ch != '\r') {
      return false;
    }
  }
  return true;
}

bool is_header(const std::vector<std::string>& fields, const std::vector<std::string_view>& expected_header) {
  if (fields.size() != expected_header.size()) {
    return false;
  }
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (fields[index] != expected_header[index]) {
      return false;
    }
  }
  return true;
}

template <typename Value>
Value parse_unsigned_integer(std::string_view text,
                             std::string_view source,
                             std::size_t line_number,
                             std::string_view field) {
  if (text.empty()) {
    throw ParseError{std::string{source}, line_number, std::string{field}, std::string{text}, "field is required"};
  }
  if (text.front() == '+' || text.front() == '-') {
    throw ParseError{std::string{source},
                     line_number,
                     std::string{field},
                     std::string{text},
                     "expected unsigned integer without sign"};
  }

  Value value{};
  const auto* const begin = text.data();
  const auto* const end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec == std::errc::result_out_of_range) {
    throw ParseError{
        std::string{source}, line_number, std::string{field}, std::string{text}, "integer is out of range"};
  }
  if (result.ec != std::errc{} || result.ptr != end) {
    throw ParseError{
        std::string{source}, line_number, std::string{field}, std::string{text}, "expected unsigned integer"};
  }
  return value;
}

Quantity parse_quantity_field(std::string_view text, std::string_view source, std::size_t line_number) {
  if (text.empty()) {
    throw ParseError{std::string{source}, line_number, "quantity", std::string{text}, "field is required"};
  }

  try {
    return parse_quantity(text);
  } catch (const std::exception& error) {
    throw ParseError{std::string{source}, line_number, "quantity", std::string{text}, error.what()};
  }
}

Side parse_side_field(std::string_view text,
                      std::string_view source,
                      std::size_t line_number,
                      std::string_view field,
                      bool allow_empty) {
  if (text.empty()) {
    if (allow_empty) {
      throw ParseError{std::string{source}, line_number, std::string{field}, std::string{text}, "empty optional side"};
    }
    throw ParseError{std::string{source}, line_number, std::string{field}, std::string{text}, "field is required"};
  }

  try {
    return parse_side(text);
  } catch (const std::exception& error) {
    throw ParseError{std::string{source}, line_number, std::string{field}, std::string{text}, error.what()};
  }
}

PriceTicks parse_price_field(std::string_view text,
                             std::string_view source,
                             std::size_t line_number,
                             const FeedParserConfig& config) {
  if (text.empty()) {
    throw ParseError{std::string{source}, line_number, "price", std::string{text}, "field is required"};
  }

  try {
    if (config.price_format == PriceFieldFormat::Decimal) {
      return price_to_ticks(text, config.tick_size);
    }
    return parse_price_ticks(text);
  } catch (const std::exception& error) {
    throw ParseError{std::string{source}, line_number, "price", std::string{text}, error.what()};
  }
}

EventKey parse_event_key(const std::vector<std::string>& fields, std::string_view source, std::size_t line_number) {
  const auto timestamp_ns =
      parse_unsigned_integer<TimestampNs>(fields[timestamp_column], source, line_number, "timestamp_ns");
  const auto sequence_id =
      parse_unsigned_integer<std::uint64_t>(fields[sequence_column], source, line_number, "sequence_id");
  return EventKey{timestamp_ns, sequence_id};
}

void validate_field_count(const std::vector<std::string>& fields,
                          std::size_t expected,
                          std::string_view source,
                          std::size_t line_number) {
  if (fields.size() != expected) {
    std::ostringstream reason;
    reason << "expected " << expected << " columns but found " << fields.size();
    throw ParseError{std::string{source}, line_number, "row", "", reason.str()};
  }
}

void validate_source_order(const std::vector<MarketEvent>& events, std::string_view source, std::size_t line_number) {
  if (events.size() < 2U) {
    return;
  }

  const auto previous = events[events.size() - 2U].key();
  const auto current = events.back().key();
  if (previous < current) {
    return;
  }

  std::ostringstream value;
  value << "timestamp_ns=" << current.timestamp_ns << ",sequence_id=" << current.sequence_id;

  std::string reason;
  if (previous == current) {
    reason = "duplicate event key; timestamp/sequence ties are rejected";
  } else {
    reason = "out-of-order event key; source order must be strictly increasing by timestamp_ns then sequence_id";
  }

  throw ParseError{std::string{source}, line_number, "event_key", value.str(), reason};
}

NormalizedMarketFeed parse_csv(std::istream& input,
                               std::string_view source,
                               const FeedParserConfig& config,
                               MarketEventType event_type) {
  validate_tick_size(config.tick_size);

  const std::vector<std::string_view> book_header{"timestamp_ns", "sequence_id", "side", "price", "quantity"};
  const std::vector<std::string_view> trade_header{
      "timestamp_ns", "sequence_id", "price", "quantity", "aggressor_side"};
  const auto& expected_header = event_type == MarketEventType::BookUpdate ? book_header : trade_header;
  const std::size_t expected_field_count = expected_header.size();

  std::vector<MarketEvent> events;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    if (is_blank(line)) {
      continue;
    }

    auto fields = split_csv_line(line);
    if (events.empty() && line_number == 1U && is_header(fields, expected_header)) {
      continue;
    }

    validate_field_count(fields, expected_field_count, source, line_number);
    const auto key = parse_event_key(fields, source, line_number);

    if (event_type == MarketEventType::BookUpdate) {
      const auto side = parse_side_field(fields[2], source, line_number, "side", false);
      const auto price_ticks = parse_price_field(fields[3], source, line_number, config);
      const auto quantity = parse_quantity_field(fields[4], source, line_number);
      events.emplace_back(BookUpdateEvent{key, side, price_ticks, quantity});
    } else {
      const auto price_ticks = parse_price_field(fields[2], source, line_number, config);
      const auto quantity = parse_quantity_field(fields[3], source, line_number);
      std::optional<Side> aggressor_side;
      if (!fields[4].empty()) {
        aggressor_side = parse_side_field(fields[4], source, line_number, "aggressor_side", false);
      }
      events.emplace_back(TradeEvent{key, price_ticks, quantity, aggressor_side});
    }

    validate_source_order(events, source, line_number);
  }

  return NormalizedMarketFeed{std::move(events), std::string{source}};
}

std::ifstream open_input_file(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input) {
    throw std::runtime_error{"failed to open input file: " + path.string()};
  }
  return input;
}

std::string side_to_string(Side side) {
  switch (side) {
    case Side::Buy:
      return "buy";
    case Side::Sell:
      return "sell";
  }
  throw std::invalid_argument("invalid Side enum value");
}

}  // namespace

ParseError::ParseError(std::string source,
                       std::size_t line_number,
                       std::string field,
                       std::string value,
                       std::string reason)
    : std::runtime_error{make_parse_error_message(source, line_number, field, value, reason)},
      source_{std::move(source)},
      line_number_{line_number},
      field_{std::move(field)},
      value_{std::move(value)},
      reason_{std::move(reason)} {}

const std::string& ParseError::source() const noexcept {
  return source_;
}

std::size_t ParseError::line_number() const noexcept {
  return line_number_;
}

const std::string& ParseError::field() const noexcept {
  return field_;
}

const std::string& ParseError::value() const noexcept {
  return value_;
}

const std::string& ParseError::reason() const noexcept {
  return reason_;
}

MarketEvent::MarketEvent(BookUpdateEvent book_update) : event_{book_update} {}

MarketEvent::MarketEvent(TradeEvent trade) : event_{trade} {}

MarketEventType MarketEvent::type() const noexcept {
  if (std::holds_alternative<BookUpdateEvent>(event_)) {
    return MarketEventType::BookUpdate;
  }
  return MarketEventType::Trade;
}

EventKey MarketEvent::key() const noexcept {
  if (const auto* book_update = std::get_if<BookUpdateEvent>(&event_)) {
    return book_update->key;
  }
  return std::get<TradeEvent>(event_).key;
}

const BookUpdateEvent& MarketEvent::book_update() const {
  return std::get<BookUpdateEvent>(event_);
}

const TradeEvent& MarketEvent::trade() const {
  return std::get<TradeEvent>(event_);
}

bool operator==(const MarketEvent& lhs, const MarketEvent& rhs) {
  return lhs.event_ == rhs.event_;
}

NormalizedMarketFeed::NormalizedMarketFeed(std::vector<MarketEvent> events, std::string source)
    : events_{std::move(events)}, source_{std::move(source)} {}

const std::vector<MarketEvent>& NormalizedMarketFeed::events() const noexcept {
  return events_;
}

bool NormalizedMarketFeed::empty() const noexcept {
  return events_.empty();
}

std::size_t NormalizedMarketFeed::size() const noexcept {
  return events_.size();
}

const std::string& NormalizedMarketFeed::source() const noexcept {
  return source_;
}

NormalizedMarketFeed parse_book_updates_csv(std::istream& input,
                                            std::string_view source,
                                            const FeedParserConfig& config) {
  return parse_csv(input, source, config, MarketEventType::BookUpdate);
}

NormalizedMarketFeed parse_trades_csv(std::istream& input, std::string_view source, const FeedParserConfig& config) {
  return parse_csv(input, source, config, MarketEventType::Trade);
}

NormalizedMarketFeed load_book_updates_csv(const std::filesystem::path& path, const FeedParserConfig& config) {
  auto input = open_input_file(path);
  return parse_book_updates_csv(input, path.string(), config);
}

NormalizedMarketFeed load_trades_csv(const std::filesystem::path& path, const FeedParserConfig& config) {
  auto input = open_input_file(path);
  return parse_trades_csv(input, path.string(), config);
}

std::string canonical_event_string(const MarketEvent& event) {
  std::ostringstream os;
  if (event.type() == MarketEventType::BookUpdate) {
    const auto& book_update = event.book_update();
    os << "BookUpdate," << book_update.key.timestamp_ns << ',' << book_update.key.sequence_id << ','
       << side_to_string(book_update.side) << ',' << book_update.price_ticks << ',' << book_update.quantity;
    return os.str();
  }

  const auto& trade = event.trade();
  os << "Trade," << trade.key.timestamp_ns << ',' << trade.key.sequence_id << ',' << trade.price_ticks << ','
     << trade.quantity << ',';
  if (trade.aggressor_side.has_value()) {
    os << side_to_string(*trade.aggressor_side);
  }
  return os.str();
}

}  // namespace replay
