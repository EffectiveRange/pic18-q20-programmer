// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <IDumper.hpp>
#include <PIC18-Q20.hpp>
#include <Region.hpp>
#include <fmt/core.h>
#include <fwd.hpp>

#include <array>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <range/v3/iterator/default_sentinel.hpp>
#include <range/v3/range/concepts.hpp>
#include <range/v3/view/chunk.hpp>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <fmt/format.h>
#include <range/v3/range/traits.hpp>
#include <range/v3/view/subrange.hpp>
#include <range/v3/view/transform.hpp>

// NOTE: the PIC18Q20 programming specification does not specify
// how are multi-byte words stored in memory (LE or BE)
// Since the intel-hex firware file has a LE representation
// all internal multi-byte words will be represented in LE format

// Utility for extracting data bits from a low level
// read transaction.
// A read transaction can contain 8 or 16 significant bits,
// 1 stop bit, and padding
// this function removes the stop bit and the front padding
template <std::unsigned_integral T, std::size_t N>
  requires(sizeof(T) <= 2 && N <= 4)
constexpr T read_cast(std::array<std::uint8_t, N> const &buff) noexcept {
  uint32_t tmp{};
  for (std::size_t i = 0; i < N; ++i) {
    tmp += (static_cast<uint8_t>(buff[i]) << i * 8);
  }
  // remove start padding
  if constexpr (sizeof(T) == 2) {
    tmp &= 0x1FFFF;
  } else {
    tmp &= 0x1FF;
  }
  // // remove stop bit;
  tmp >>= 1;
  return static_cast<T>(tmp);
}

// This utlity function converts a read data elem (either 1 or 2 bytes)
// into an array, where a 2 byte word will be represented in little endian
// layout - lower array index holds the LSB
template <std::size_t NOUT, std::size_t NIN>
  requires(NOUT == 1 || NOUT == 2)
constexpr auto read_cast(std::array<std::uint8_t, NIN> const &buff) noexcept {
  auto tmp = read_cast<uint16_t>(buff);
  std::array<std::uint8_t, NOUT> res{};
  for (auto it = res.begin(); it != res.end(); ++it, tmp >>= 8) {
    *it = static_cast<std::uint8_t>(0xFF & tmp);
  }
  return res;
}

// This function converts a span of 2 elements into a builtin integer
// type
template <std::unsigned_integral T, std::unsigned_integral U>
  requires(sizeof(T) == 2 && sizeof(U) == 1)
constexpr inline auto span_cast(std::span<U, 2> s) noexcept {
  uint16_t res{};
  res = s[0];
  res += (s[1] << 8);
  return res;
}

template <typename R>
concept byte_range =
    rg::input_range<std::remove_reference_t<R>> &&
    rg::sized_range<std::remove_reference_t<R>> &&
    std::unsigned_integral<rg::range_value_t<std::remove_reference_t<R>>> &&
    sizeof(rg::range_value_t<std::remove_reference_t<R>>) == 1;

// cast a byte range into a builtin integer, where the byte range
// holds a representation in little endian format
template <std::unsigned_integral T, byte_range R>
constexpr auto range_cast(R &&r) noexcept {
  T tmp{};
  auto it = rg::begin(r);
  const auto end = rg::end(r);
  for (size_t i = 0; i != sizeof(T) && it != end; ++i, ++it) {
    tmp += *it << i * 8;
  }
  return tmp;
}

// For representing layout of raw memory in compile-time
struct IdxPair {
  std::size_t offset;
  std::size_t extent;
  constexpr auto last() const noexcept { return offset + extent; }
};

template <std::size_t f, std::size_t l>
using IdxPair_t = std::integral_constant<IdxPair, IdxPair{f, l}>;

template <std::size_t f, std::size_t l>
constexpr auto IdxPair_v = IdxPair_t<f, l>{};

namespace detail {

// This concept helper check for that
// the declarative layout description using idxpairs
// is sane (no overlaps)
constexpr bool ordered() noexcept { return true; }

constexpr bool ordered(auto) noexcept { return true; }

template <typename T1, typename T2, typename... Ts>
constexpr bool ordered(T1 t1, T2 t2, Ts... ts) noexcept {
  return t1.offset + t1.extent <= t2.offset && ordered(ts...);
}

/// type helper for mapping from Idx to parsed type
// a 2 byte Idx slice => uint16
// a N*2 byte (N >1) Idx slice => array<uint16_t, N/2>
template <IdxPair p> struct parsed_type;

template <IdxPair p>
  requires(p.extent == 2)
struct parsed_type<p> {
  using type = uint16_t;
};

template <IdxPair p>
  requires(p.extent > 2 && p.extent % 2 == 0)
struct parsed_type<p> {
  using type = std::array<uint16_t, p.extent / 2>;
};

// concept helper for getting the last offset given an IdxPair based slicing
template <typename... Ts> constexpr std::size_t last_idx(Ts... idx) noexcept {
  return ((idx.last()), ...);
}

template <typename T, size_t N>
  requires(N == 2)
uint16_t parse(std::span<T, N> s) {
  return span_cast<uint16_t>(s);
}

template <IdxPair...> struct IdxRange {};

template <typename T> struct idx_sequence;

template <size_t... Is> struct idx_sequence<std::index_sequence<Is...>> {
  using type = IdxRange<IdxPair{Is * 2, 2}...>;
};

template <typename T>
using make_idxpair_sequence = typename idx_sequence<T>::type;

template <std::unsigned_integral T, size_t N, IdxPair... Idxs>
auto parse_array(std::span<T, N> s, IdxRange<Idxs...>) {
  return std::array<uint16_t, N / 2>{
      parse(s.template subspan<Idxs.offset, Idxs.extent>())...};
}

template <typename T, size_t N>
  requires(N > 2 && N != std::dynamic_extent && N % 2 == 0)
std::array<uint16_t, N / 2> parse(std::span<T, N> s) {
  return parse_array(s,
                     make_idxpair_sequence<std::make_index_sequence<N / 2>>{});
}

} // namespace detail

template <IdxPair... p>
using parsed_type_t = std::tuple<typename detail::parsed_type<p>::type...>;

using detail::IdxRange;

template <std::unsigned_integral T, std::size_t N, IdxPair... idxs>
  requires(sizeof(T) == 1 && detail::ordered(idxs...) &&
           detail::last_idx(idxs...) <= N)
parsed_type_t<idxs...> parse_cast(std::span<T, N> s, IdxRange<idxs...>) {
  return std::make_tuple(
      detail::parse(s.template subspan<idxs.offset, idxs.extent>())...);
}

// Converts a primitive type to transmission format
// MSB -> LSB, adding in a stop bit as well
template <std::unsigned_integral T>
  requires(sizeof(T) <= 4)
constexpr auto write_cast(T val) noexcept {
  assert((val & 0x8000'0000) == 0);
  uint32_t tmp = val;
  // add stop bit
  tmp <<= 1;
  std::array<std::uint8_t, 3> buff{};
  buff[0] = std::uint8_t((tmp & 0x00'FF'00'00) >> 16);
  buff[1] = std::uint8_t((tmp & 0x00'00'FF'00) >> 8);
  buff[2] = std::uint8_t((tmp & 0x00'00'00'FF));
  return buff;
}

struct dword_format {
  static constexpr auto blank_fmt() { return "        "sv; }
  static constexpr auto fmt() { return "{:08x}"sv; }
};

struct word_format {
  static constexpr auto blank_fmt() { return "    "sv; }
  static constexpr auto fmt() { return "{:04x}"sv; }
};
struct byte_format {
  static constexpr auto blank_fmt() { return "  "sv; }
  static constexpr auto fmt() { return "{:02x}"sv; }
};

struct select_format {
  explicit select_format(uint32_t word_size) {
    switch (word_size) {
    case 1:
      m_format = byte_format{};
      break;
    case 2:
      m_format = word_format{};
      break;
    case 4:
      m_format = dword_format{};
      break;
    default:
      throw std::out_of_range("unsupported word size");
    }
  }

  std::string_view blank_fmt() const {
    return std::visit([]<typename T>(T const &f) { return T::blank_fmt(); },
                      m_format);
  }

  std::string_view fmt() const {
    return std::visit([]<typename T>(T const &f) { return T::fmt(); },
                      m_format);
  }

private:
  std::variant<byte_format, word_format, dword_format> m_format;
};

template <size_t N> struct select_int;

template <> struct select_int<1> {
  using type = std::uint8_t;
};
template <> struct select_int<2> {
  using type = std::uint16_t;
};
template <> struct select_int<4> {
  using type = std::uint32_t;
};

template <size_t N> using select_int_t = typename select_int<N>::type;

struct OstreamDumper : IDumper {
  void dump_start() override {}
  void dump_end() override {}
  explicit OstreamDumper(std::ostream &os, std::size_t bytes_per_line = 16)
      : os{os}, bytes_per_line{bytes_per_line} {}

  void dump_region(Address::Region reg, std::span<uint8_t> data) override {
    dump_memory_region(reg, data);
  }

  template <std::ranges::forward_range Rng>
    requires(std::integral<rg::range_value_t<Rng>> &&
             sizeof(rg::range_value_t<Rng>) == 1)
  void dump_memory_region(Address::Region reg, Rng &&data) {
    using T = rg::range_value_t<Rng>;
    const auto base = Address::with_region(
        reg,
        [this]<Address::region R>(auto idx, Address::region_t<R>) {
          os << R << '\n';
          return R.start;
        },
        pic18fq20);
    dump_memory(base, data);
  }

  template <std::ranges::forward_range Rng>
    requires(std::integral<rg::range_value_t<Rng>> &&
             sizeof(rg::range_value_t<Rng>) == 1)
  void dump_memory(uint32_t addr, Rng &&data) {
    for (auto &&line : data | ranges::views::chunk(bytes_per_line)) {
      dump_line(addr, line);
      addr += bytes_per_line;
      os << '\n';
    }
  }

  template <std::ranges::forward_range Rng>
    requires(std::integral<rg::range_value_t<Rng>> &&
             sizeof(rg::range_value_t<Rng>) == 1)
  void dump_line(uint32_t addr, Rng &&data) {
    using T = rg::range_value_t<Rng>;
    std::ostream_iterator<char> out(os);
    constexpr auto &addr_format = "0x{:06x} | ";
    fmt::format_to(out, addr_format, addr);
    const auto data_size = dump_data_padded(out, addr, data);
    fmt::format_to(out, "| ");
    dump_ascii_padded(out, data_size, data);
    fmt::format_to(out, " |");
  }

private:
  template <std::ranges::forward_range Rng>
    requires std::integral<rg::range_value_t<Rng>>
  auto dump_data_padded(std::ostream_iterator<char> out, uint32_t addr,
                        Rng &&data) {
    size_t i = 0;
    using T = rg::range_value_t<Rng>;
    select_format format(1);

    for (auto val : data) {
      fmt::vformat_to(out, format.fmt(), fmt::make_format_args(val));
      fmt::format_to(out, " ");
      ++i;
    }
    for (auto pad = i; pad < bytes_per_line; ++pad) {
      fmt::vformat_to(out, format.blank_fmt(), {});
      fmt::format_to(out, " ");
    }
    return i;
  }

  template <std::ranges::forward_range Rng>
    requires std::integral<rg::range_value_t<Rng>>
  auto dump_ascii_padded(std::ostream_iterator<char> out, std::size_t data_size,
                         Rng &&data) {
    using T = rg::range_value_t<Rng>;
    select_format format(1);
    const auto blank = format.blank_fmt();

    for (auto v : data) {
      for (size_t idx = 0; idx < sizeof(v); ++idx) {
        const int val = (v >> (sizeof(v) - 1 - idx) * 8) & 0xFF;
        os << (isprint(val) ? static_cast<char>(val) : '.');
      }
    }

    for (auto pad = data_size; pad < bytes_per_line; ++pad) {
      fmt::vformat_to(out, blank.substr(0, blank.size() / 2), {});
    }
  }
  //////////////
  std::ostream &os;
  std::size_t bytes_per_line{};
};
