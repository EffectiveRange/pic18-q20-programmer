// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#include "PIC18-Q20.hpp"
#include "Region.hpp"
#include <IntelHex.hpp>

#include <fmt/format.h>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/view/chunk.hpp>

using namespace IntelHex;

void Dumper::dump_region(Address::Region reg, std::span<uint8_t> data) {
  using std::literals::operator""sv;
  const auto base_addr = Address::with_region(
      reg, [](auto idx, auto reg) { return reg.value.start; }, pic18fq20);

  dump_data_memory(base_addr, data);
}
void Dumper::dump_data_memory(uint32_t base_addr,
                              std::span<uint8_t const> data_span) {
  const auto addr_hi = (base_addr & 0xFF0000) >> 16;
  std::ostream_iterator<char> oit(os);
  if (base_addr > 0xFFFF) {
    const auto chk = 2 + 4 + addr_hi;
    oit = fmt::format_to(oit, lineformat, 2, 0, 4);
    oit = fmt::format_to(oit, "{:04X}", addr_hi);
    oit = fmt::format_to(oit, "{:02X}\n", extended_linear_addr_chk(addr_hi));
  }
  auto addr_lo = (base_addr & 0xFFFF);
  for (auto &&line : data_span | rgv::chunk(16)) {
    // swap endiannes if needed here
    std::array<uint8_t, 16> tmp;
    auto end = rg::copy(line, tmp.begin());
    std::span<uint8_t const> data_span(tmp.begin(), rg::size(line));
    dump_data_line(addr_lo, data_span);
    addr_lo += data_span.size();
  }
}
void Dumper::dump_data_line(uint16_t addr_lo,
                            std::span<uint8_t const> data_span) {
  std::ostream_iterator<char> oit(os);
  const auto chk = data_chk(addr_lo, data_span);
  oit = fmt::format_to(oit, lineformat, data_span.size(), addr_lo, 0);
  for (auto byte : data_span) {
    oit = fmt::format_to(oit, "{:02X}", byte);
  }
  oit = fmt::format_to(oit, "{:02X}\n", data_chk(addr_lo, data_span));
}
