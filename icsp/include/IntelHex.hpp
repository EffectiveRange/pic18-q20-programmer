// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once
#include <charconv>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <optional>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/subrange.hpp>
#include <range/v3/view/transform.hpp>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "FimwareFile.hpp"
#include "IDumper.hpp"
#include "Region.hpp"

#include <fmt/format.h>

namespace IntelHex {
enum class RecordType : uint8_t {
  DATA = 0x00,
  END_OF_FILE = 0x01,
  EXTENDED_LIN_ADDR = 0x04

};

inline constexpr auto toRecordType(std::underlying_type_t<RecordType> val) {
  switch (val) {
  case static_cast<std::underlying_type_t<RecordType>>(RecordType::DATA):
    return RecordType::DATA;
  case static_cast<std::underlying_type_t<RecordType>>(RecordType::END_OF_FILE):
    return RecordType::END_OF_FILE;
  case static_cast<std::underlying_type_t<RecordType>>(
      RecordType::EXTENDED_LIN_ADDR):
    return RecordType::EXTENDED_LIN_ADDR;
  default:
    throw std::invalid_argument("Unhandled RecordType");
  }
}

template <typename Enum> inline constexpr auto to_underlying(Enum e) {
  return static_cast<std::underlying_type_t<Enum>>(e);
}

// Format:
// <StartCode><ByteCount><Address><Record type><Data><Checksum>
inline const auto &hex_line_re() {
  static const std::regex re(
      R"--(:([0-9a-fA-F]{2})([0-9a-fA-F]{4})([0-9a-fA-F]{2})((?:[0-9a-fA-F]{2})+))--");
  return re;
}

template <std::integral T, std::contiguous_iterator It>
  requires std::same_as<char, std::remove_cvref_t<std::iter_value_t<It>>>
inline T parse_int(It first, It last, int base = 10) {
  T val{};
  const auto f = std::addressof(*first);
  const auto l = std::next(f, std::distance(first, last));
  auto [ptr, ec] = std::from_chars(f, l, val, base);
  if (ec == std::errc() && ptr == l) {
    return val;
  }
  throw std::system_error(std::make_error_code(
      ec != std::errc() ? ec : std::errc::argument_out_of_domain));
}

template <std::integral T>
inline T parse_int(std::ssub_match match, int base = 10) {
  return parse_int<T>(match.first, match.second, base);
}

using static_vec = std::pair<std::size_t, std::array<uint8_t, 256>>;

inline auto parse_payload(std::ssub_match p) {
  static_vec res;
  auto bytes =
      rg::subrange(p.first, p.second) | rgv::chunk(2) |
      rgv::transform([](auto &&chunk) {
        return parse_int<uint8_t>(rg::begin(chunk), rg::end(chunk), 16);
      });
  auto end = rg::copy(bytes, rg::begin(res.second));
  res.first = std::distance(rg::begin(res.second), end.out);
  return res;
}

struct hex_line {
  uint8_t len;
  uint16_t addr;
  RecordType record_type;
  static_vec payload;
};

inline void validate_checksum(std::string const &line, hex_line &res) {
  const auto addr_hi = (res.addr & 0xFF00) >> 8;
  const auto addr_lo = (res.addr & 0xFF);
  const uint32_t accu =
      res.len + addr_hi + addr_lo + to_underlying(res.record_type);

  auto &[size, data] = res.payload;
  const auto chk_sum =
      std::accumulate(data.begin(), std::next(data.begin(), size), accu);
  const auto chk = chk_sum & 0xFF;
  if (chk != 0) {
    throw std::runtime_error(
        fmt::format("Invalid checksum (0x{:02x}) on line {}", chk, line));
  }
  // trim checksum from payload data
  size -= 1;
}

inline std::optional<hex_line> parse_hex_line(std::istream &is) {
  std::string line;
  if (!std::getline(is, line)) {
    if (!is.eof()) {
      throw std::runtime_error("input stream failure");
    }
    return std::nullopt;
  }
  if (line.ends_with('\r')) {
    line.pop_back();
  }
  std::smatch line_match;
  if (!std::regex_match(line, line_match, hex_line_re())) {
    throw std::runtime_error(fmt::format("Invalid line in hex file:{}", line));
  }
  auto res = std::make_optional<hex_line>();
  res->len = parse_int<uint8_t>(line_match[1], 16);
  res->addr = parse_int<uint16_t>(line_match[2], 16);
  res->record_type = toRecordType(parse_int<uint8_t>(line_match[3], 16));
  res->payload = parse_payload(line_match[4]);
  validate_checksum(line, *res);
  return res;
}

template <typename Map>
inline auto parse_hex_file(Map map, std::istream &is,
                           bool little_endian = true) {
  Firmware result;
  auto line = parse_hex_line(is);
  std::optional<uint32_t> base_addr{};
  while (line) {
    switch (line->record_type) {
    case RecordType::DATA:
      base_addr = process_init_record(base_addr, line.value(), result, map);
      process_data_record(line.value(), result, map, little_endian);
      break;
    case RecordType::EXTENDED_LIN_ADDR:
      base_addr = process_extended_address_record(line.value(), result, map);
      break;
    case RecordType::END_OF_FILE: {
      return result;
    }
    default:
      throw std::runtime_error(fmt::format("Unhandled record_type {}",
                                           to_underlying(line->record_type)));
    };
    line = parse_hex_line(is);
  }
  throw std::runtime_error("End-of-file missing from hex file");
};

class Dumper : public IDumper {
public:
  inline static constexpr auto lineformat = ":{:02X}{:04X}{:02X}"sv;

  void dump_start() override {}
  void dump_end() override { os << ":00000001FF\n"; }
  void dump_region(Address::Region reg, std::span<uint8_t> data) override;
  explicit Dumper(std::ostream &os, bool little_endian = true)
      : os{os}, little_endian{little_endian} {}

  static uint8_t extended_linear_addr_chk(uint16_t addr_hi) noexcept {
    const auto base_chk = uint32_t{2} + uint32_t{4} + (addr_hi & 0xFF) +
                          ((addr_hi & 0xFF00) >> 8);
    return static_cast<uint8_t>((~base_chk + 1) & 0xFF);
  }

  static uint8_t data_chk(uint16_t addr_lo,
                          std::span<uint8_t const> data) noexcept {
    const auto base_chk = std::accumulate(data.begin(), data.end(),
                                          data.size() + (addr_lo & 0xFF) +
                                              ((addr_lo & 0xFF00) >> 8),
                                          std::plus<>{});
    const auto chk = (~((base_chk & 0xFF))) + 1;
    return static_cast<uint8_t>(chk & 0xFF);
  }

  void dump_data_line(uint16_t addr_lo, std::span<uint8_t const> data);
  void dump_data_memory(uint32_t base_addr, std::span<uint8_t const> data);

private:
  std::ostream &os;
  bool little_endian{};
};
template <typename Map>
auto process_init_record(std::optional<uint32_t> base_addr,
                         const IntelHex::hex_line &line, Firmware &result,
                         Map map) {
  if (result.empty() || base_addr.has_value()) {
    const auto addr = static_cast<uint32_t>(base_addr.value_or(0)) +
                      static_cast<uint32_t>(line.addr);
    Address::with_region(
        addr,
        [&]<Address::region region>(auto idx, Address::region_t<region>) {
          result.emplace_back(region, base_addr.value_or(0));
        },
        map);
    result.back().elems.emplace_back(addr);
  }
  return std::nullopt;
}

inline void ensure_non_overlapping(uint32_t line_addr, uint32_t linear_addr,
                                   uint32_t expected_addr) {
  if (linear_addr < expected_addr) {
    throw std::runtime_error(fmt::format(
        "Overlapping layout on line with addr:0x{:04x}, linear addr: 0x{:08x}",
        line_addr, linear_addr));
  }
}

inline void ensure_in_bounds(Address::region const &region, uint32_t line_addr,
                             uint32_t linear_addr) {

  if (linear_addr >= region.end || linear_addr < region.start) {
    throw std::runtime_error(
        fmt::format("Out of bounds data on line with addr:0x{:04x}, linear "
                    "addr: 0x{:08x}",
                    line_addr, linear_addr));
  }
}

inline void append_data(const IntelHex::hex_line &line, Firmware &result,
                        bool little_endian) {
  auto &curr_elem = result.back().elems.back();
  const auto &[size, payload] = line.payload;
  if (const auto word_size = result.back().region.word_size;
      word_size == 1 || little_endian) {
    curr_elem.data.insert(curr_elem.data.end(), payload.begin(),
                          payload.begin() + size);
  } else {
    auto pr = rg::make_subrange(payload.begin(), payload.begin() + size);
    auto &&reversed =
        pr | rgv::chunk(word_size) | rgv::transform(rgv::reverse) | rgv::join;
    rg::copy(reversed, std::back_inserter(curr_elem.data));
  }
}

template <typename Map>
auto process_data_record(const IntelHex::hex_line &line, Firmware &result,
                         Map map, bool little_endian) {
  auto &curr_region = result.back().region;
  auto &curr_elem = result.back().elems.back();
  const auto linear_addr = result.back().base_addr + line.addr;
  const auto expected_addr = curr_elem.base_addr + curr_elem.data.size();
  ensure_non_overlapping(line.addr, linear_addr, expected_addr);
  ensure_in_bounds(result.back().region, line.addr, linear_addr);
  if (expected_addr != linear_addr) {
    result.back().elems.emplace_back(linear_addr);
  }
  append_data(line, result, little_endian);
}

template <typename Map>
auto process_extended_address_record(const IntelHex::hex_line &line,
                                     Firmware &result, Map map) {
  const auto &[size, payload] = line.payload;
  if (size != 2) {
    throw std::runtime_error(
        "Invalid payload length for extended address record");
  }
  const auto addr_prefix =
      (static_cast<uint32_t>(line.payload.second[0]) << 8) +
      static_cast<uint32_t>(line.payload.second[1]);
  const auto base_addr = addr_prefix << 16;
  return base_addr;
}

} // namespace IntelHex
