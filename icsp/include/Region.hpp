// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <fwd.hpp>

#include <fmt/format.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

// TODO: make these configurable...
namespace Address {
enum class Region {
  INVALID = 0,
  PROGRAM = 1,
  USER = 1 << 1,
  DIA = 1 << 2,
  CONFIG = 1 << 3,
  EEPROM = 1 << 4,
  DCI = 1 << 5,
  ID = 1 << 6,
  EEPROM_PROG = EEPROM | PROGRAM,
  EEPROM_USER = EEPROM | USER,
  EEPROM_CONFIG = EEPROM | CONFIG,
  PROG_USER = PROGRAM | USER,
  PROG_CONFIG = PROGRAM | CONFIG,
  USER_CONFIG = USER | CONFIG,
  EEPROM_PROG_USER = EEPROM | PROGRAM | USER,
  EEPROM_PROG_CONFIG = EEPROM | PROGRAM | CONFIG,
  EEPROM_USER_CONFIG = EEPROM | USER | CONFIG,
  PROG_USER_CONFIG = PROGRAM | USER | CONFIG,
  EEPROM_PROG_USER_CONFIG = EEPROM | PROGRAM | USER | CONFIG,
};

constexpr Region operator|(Region r1, Region r2) noexcept {
  using ut = std::underlying_type_t<Region>;
  return static_cast<Region>(static_cast<ut>(r1) | static_cast<ut>(r2));
}

constexpr Region operator&(Region r1, Region r2) noexcept {
  using ut = std::underlying_type_t<Region>;
  return static_cast<Region>(static_cast<ut>(r1) & static_cast<ut>(r2));
}

inline constexpr std::string_view region_to_string(Address::Region region) {
  switch (region) {
  case Address::Region::PROGRAM:
    return "PROGRAM"sv;
  case Address::Region::USER:
    return "USER"sv;
  case Address::Region::DIA:
    return "DIA"sv;
  case Address::Region::CONFIG:
    return "CONFIG"sv;
  case Address::Region::EEPROM:
    return "EEPROM"sv;
  case Address::Region::DCI:
    return "DCI"sv;
  case Address::Region::ID:
    return "ID"sv;
  default:
    return "UNKNOWN"sv;
  }
}

inline constexpr Address::Region string_to_region(std::string_view str) {
  if (str == "PROGRAM"sv) {
    return Address::Region::PROGRAM;
  } else if (str == "USER"sv) {
    return Address::Region::USER;
  } else if (str == "DIA"sv) {
    return Address::Region::DIA;
  } else if (str == "CONFIG"sv) {
    return Address::Region::CONFIG;
  } else if (str == "EEPROM"sv) {
    return Address::Region::EEPROM;
  } else if (str == "DCI"sv) {
    return Address::Region::DCI;
  } else if (str == "ID"sv) {
    return Address::Region::ID;
  } else {
    throw std::invalid_argument("Invalid region string");
  }
}

struct region {
  Region name;
  uint32_t start{};
  uint32_t end{};
  uint32_t word_size{};
  uint32_t t_PROG_us{};
  bool writable{};
  bool autoincrement_addr{true};

  // TODO: verify start and end is integral multiple of word_size

  constexpr auto size() const {
    return end >= start ? end - start
                        : throw std::out_of_range("end must be geq to start");
  }
  constexpr auto prog_delay() const noexcept {
    return writable ? std::make_optional(std::chrono::microseconds{t_PROG_us})
                    : std::optional<std::chrono::microseconds>{};
  }
  constexpr auto word_cnt() const noexcept { return size() / word_size; }
  constexpr auto rel_addr(uint32_t addr) const {
    return addr >= start && addr < end
               ? addr - start
               : throw std::out_of_range("Address out of range");
  }
  constexpr auto name_str() const noexcept { return region_to_string(name); }
};

struct IRegion {
  using Ptr = std::unique_ptr<IRegion>;
  virtual Region name() const noexcept = 0;
  virtual std::string_view name_str() const = 0;
  virtual std::pair<uint32_t, uint32_t> address() const noexcept = 0;
  virtual uint32_t word_size() const noexcept = 0;
  virtual std::chrono::microseconds prog_delay() const noexcept = 0;
  virtual bool writable() const noexcept = 0;
  virtual bool autoincrement_addr() const noexcept = 0;
  virtual ~IRegion() = default;
};

inline std::ostream &operator<<(std::ostream &os, const region &r) {
  return os << fmt::format(
             "Region name:{} address:[{:06x}h,{:06x}h)  word size: {}",
             region_to_string(r.name), r.start, r.end, r.word_size);
}

template <typename T> struct is_region : std::false_type {};

template <> struct is_region<region> : std::true_type {};

template <typename T>
concept is_region_c = is_region<std::remove_cvref_t<T>>::value;

namespace detail {
template <is_region_c Reg, Reg R> struct IRegionImpl : IRegion {

  std::string_view name_str() const override {
    return region_to_string(R.name);
  }
  Region name() const noexcept override { return R.name; }
  std::pair<uint32_t, uint32_t> address() const noexcept override {
    return {R.start, R.end};
  }
  uint32_t word_size() const noexcept override { return R.word_size; }

  std::chrono::microseconds prog_delay() const noexcept override {
    return R.prog_delay().value();
  }

  bool writable() const noexcept override { return R.writable; };
  bool autoincrement_addr() const noexcept override {
    return R.autoincrement_addr;
  }
};

} // namespace detail
template <is_region_c Reg, Reg R>
std::unique_ptr<IRegion> make_region(constant<Reg, R>) {
  return std::make_unique<detail::IRegionImpl<Reg, R>>();
}
template <auto R>
requires is_region_c<decltype(R)>
using region_t = constant<std::remove_const_t<decltype(R)>, R>;

template <region _region> struct region_data {

  constexpr auto region() const noexcept { return _region; }
  constexpr auto base_addr() const noexcept { return _region.start; }
  constexpr auto word_size() const noexcept { return _region.word_size; }
  constexpr auto name() const noexcept {
    return region_to_string(_region.name);
  }
  constexpr std::span<uint8_t const, _region.size()> view() const noexcept {
    return data;
  }
  std::array<uint8_t, _region.size()> data{};
};

namespace detail {
constexpr bool region_in_order() noexcept { return true; }
template <auto R1> constexpr bool region_in_order(region_t<R1>) noexcept {
  return true;
}
template <auto R1, auto R2, auto... Rs>
constexpr bool region_in_order(region_t<R1>, region_t<R2>,
                               region_t<Rs>... rs) noexcept {
  return R1.end <= R2.start && region_in_order(rs...);
}
} // namespace detail

template <auto... Rs> struct RegionMap {
  static_assert(detail::region_in_order(region_t<Rs>{}...),
                "Region map must be in order of increasing memory addresses");
};

namespace detail {
template <typename KeyT, typename CompF, typename F, size_t Idx, auto R,
          auto... Rs>
requires std::invocable<F, std::integral_constant<size_t, Idx>, region_t<R>>
auto with_region(KeyT key, CompF comp, F f,
                 std::integral_constant<size_t, Idx> idx, RegionMap<R, Rs...>)
    -> std::invoke_result_t<F, decltype(idx), region_t<R>> {

  if (comp(key, R)) {
    return f(idx, region_t<R>{});
  }
  if constexpr (sizeof...(Rs) > 0) {
    return with_region(key, comp, std::move(f),
                       std::integral_constant<size_t, Idx + 1>{},
                       RegionMap<Rs...>{});
  } else {
    throw std::out_of_range("out of bounds address");
  }
}

} // namespace detail

template <typename F, auto... Rs>
decltype(auto) with_region(uint32_t addr, F f, RegionMap<Rs...> rs) {
  return detail::with_region(
      addr, [](auto a, auto r) { return a >= r.start && a < r.end; }, f,
      std::integral_constant<size_t, 0>{}, rs);
}

template <typename F, auto... Rs>
decltype(auto) with_region(Region name, F f, RegionMap<Rs...>) {
  return detail::with_region(
      name, [](auto n, auto r) { return n == r.name; }, std::move(f),
      std::integral_constant<size_t, 0>{}, RegionMap<Rs...>{});
}

template <typename F, auto... Rs>
decltype(auto) with_region(std::string_view name, F f, RegionMap<Rs...>) {
  return with_region(string_to_region(name), std::move(f), RegionMap<Rs...>{});
}

} // namespace Address
