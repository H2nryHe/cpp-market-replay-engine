#include "replay/fill.hpp"

#include <sstream>

namespace replay {

std::string canonical_fill_string(const Fill& fill) {
  std::ostringstream os;
  os << fill.fill_sequence_id << ',' << fill.order_id << ','
     << (fill.side == Side::Buy ? "buy" : "sell") << ',' << fill.price_ticks << ',' << fill.quantity << ','
     << fill.fill_timestamp_ns << ',' << fill.fee_amount;
  return os.str();
}

}  // namespace replay
