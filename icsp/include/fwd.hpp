// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ratio>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <range/v3/algorithm/copy.hpp>
#include <range/v3/algorithm/equal.hpp>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/common.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/for_each.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/transform.hpp>

namespace rg = ranges;
namespace rgv = rg::views;

using std::literals::string_literals::operator""s;
using std::literals::string_view_literals::operator""sv;

inline constexpr auto operator""_b(unsigned long long val) {
  return std::uint8_t(val);
}

template <std::invocable F> struct [[nodiscard]] finally : F {
  constexpr explicit finally(F f) : F{std::move(f)} {}
  constexpr ~finally() { (*static_cast<F *>(this))(); }
};

template <typename T, T val> struct constant {
  using type = T;
  inline static constexpr type value = val;
};

template <typename T> struct TD;