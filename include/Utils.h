#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

std::string SHA1(std::string_view data);

std::string URLEncode(std::string_view data);