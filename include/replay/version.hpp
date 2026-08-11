#pragma once

#include <string_view>

namespace replay {

std::string_view engine_name() noexcept;
std::string_view engine_version() noexcept;

}  // namespace replay
