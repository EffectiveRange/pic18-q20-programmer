// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "PIC18-Q20.hpp"
#include "Region.hpp"
#include "utils.hpp"
#include <FimwareFile.hpp>
#include <ICSP_header.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <range/v3/algorithm/transform.hpp>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/range/access.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <string_view>

#include <fmt/format.h>

struct DCI {
  uint16_t erase_page_size{};
  uint16_t num_erasable_pages{};
  uint16_t eeprom_size{};
  uint16_t pin_cnt{};
};

struct DeviceId {
  uint16_t deviceId{};
  uint16_t revisionId{};
  constexpr std::string_view deviceIdStr() const noexcept {
    using std::literals::operator""sv;
    switch (deviceId) {
    case 0x7ae0:
      return "PIC18F04Q20"sv;
    case 0x7AA0:
      return "PIC18F05Q20"sv;
    case 0x7A60:
      return "PIC18F06Q20"sv;
    case 0x7AC0:
      return "PIC18F14Q20"sv;
    case 0x7A80:
      return "PIC18F15Q20"sv;
    case 0x7A40:
      return "PIC18F16Q20"sv;
    default:
      return "Unknown"sv;
    }
  }
  std::string revisionStr() const {
    const auto major = (revisionId & 0xFC0) >> 6;
    const auto minor = (revisionId & 0x3F);
    const auto majorRev = static_cast<char>(static_cast<int>('A') + major);
    return fmt::format("{}{}", majorRev, minor);
  }
};

struct TempCoeffs {
  uint16_t gain{};
  uint16_t adc_90{};
  uint16_t offset{};

  float gain_val() const noexcept { return 256 * 0.1f / gain; }
};

struct DIA {
  std::array<uint16_t, 9> mchp_uid{};
  std::array<uint16_t, 8> ext_uid{};
  TempCoeffs low_temp_coeffs;
  TempCoeffs high_temp_coeffs;
  std::array<uint16_t, 3> fixed_voltage_ref{};
  std::array<uint16_t, 3> fixed_voltage_comp{};

  using idx = IdxPair;
  using layout =
      IdxRange<idx{0, 18}, idx{20, 16}, idx{36, 2}, idx{38, 2}, idx{40, 2},
               idx{42, 2}, idx{44, 2}, idx{46, 2}, idx{48, 2}, idx{50, 2},
               idx{52, 2}, idx{54, 2}, idx{56, 2}, idx{58, 2}>;

  template <typename T, size_t N>
  static DIA parse(std::span<T, N> sp) noexcept {
    auto res = parse_cast(sp, layout{});
    DIA dia{};
    dia.mchp_uid = std::get<0>(res);
    dia.ext_uid = std::get<1>(res);
    dia.low_temp_coeffs.gain = std::get<2>(res);
    dia.low_temp_coeffs.adc_90 = std::get<3>(res);
    dia.low_temp_coeffs.offset = std::get<4>(res);
    dia.high_temp_coeffs.gain = std::get<5>(res);
    dia.high_temp_coeffs.adc_90 = std::get<6>(res);
    dia.high_temp_coeffs.offset = std::get<7>(res);
    dia.fixed_voltage_ref[0] = std::get<8>(res);
    dia.fixed_voltage_ref[1] = std::get<9>(res);
    dia.fixed_voltage_ref[2] = std::get<10>(res);
    dia.fixed_voltage_comp[0] = std::get<11>(res);
    dia.fixed_voltage_comp[1] = std::get<12>(res);
    dia.fixed_voltage_comp[2] = std::get<13>(res);
    return dia;
  }
};

template <typename Map> class PICProgrammer : private Map {
public:
  explicit PICProgrammer(Map map, ICSPHeader &icsp)
      : Map{std::move(map)}, icsp{icsp},
        prog_guard{this->icsp.enter_programming()} {}

  explicit PICProgrammer(Map map, ICSPHeader &icsp, ICSPHeader::ExitProg adopt)
      : Map{std::move(map)}, icsp{icsp}, prog_guard{std::move(adopt)} {
    assert(icsp.programming());
  }

  DCI read_dci() {
    const auto region_data = icsp.read_region(pic18q20map::dci_region);
    DCI dci{};
    auto s = region_data.view();
    dci.erase_page_size = span_cast<uint16_t>(s.subspan<0, 2>());
    dci.num_erasable_pages = span_cast<uint16_t>(s.subspan<4, 2>());
    dci.eeprom_size = span_cast<uint16_t>(s.subspan<6, 2>());
    dci.pin_cnt = span_cast<uint16_t>(s.subspan<8, 2>());
    return dci;
  }

  const auto &map() const noexcept { return static_cast<const Map &>(*this); }

  DeviceId read_device_id() const {
    const auto rd = icsp.read_region(pic18q20map::id_region);

    return DeviceId{span_cast<uint16_t>(rd.view().template subspan<2, 2>()),
                    span_cast<uint16_t>(rd.view().template subspan<0, 2>())};
  }

  DIA read_dia() const {
    auto region_data = icsp.read_region(pic18q20map::dia_region);
    return DIA::parse(region_data.view());
  };

  void program_verify(Firmware const &fw,
                      Address::Region extra_erase = Address::Region::INVALID) {
    const auto regions_to_erase = erasable_regions(fw, extra_erase);
    icsp.bulk_erase(regions_to_erase);
    write_verify_region(fw, Address::Region::PROGRAM);
    write_verify_region(fw, Address::Region::EEPROM);
    write_verify_region(fw, Address::Region::USER);
    write_verify_region(fw, Address::Region::CONFIG);
  }

  Address::Region
  erasable_regions(Firmware const &fw,
                   Address::Region init = Address::Region::INVALID) {
    return rg::accumulate(
        fw, init, std::bit_or<>{},
        [](FirmwareFileRegion const &r) { return r.region.name; });
  }

private:
  void write_verify_region(Firmware const &fw, Address::Region reg) {
    for (FirmwareFileRegion const &r : filter_region(fw, reg)) {
      for (FirmwareFileRegionElem const &elem : r.elems) {
        icsp.write_verify(map(), elem.base_addr, elem.data.begin(),
                          elem.data.end());
      }
    }
  }

  auto filter_region(Firmware const &fw, Address::Region reg) {
    return fw | rgv::filter([reg](FirmwareFileRegion const &r) {
             return r.region.name == reg;
           });
  }

  auto word_view(std::vector<std::uint8_t> const &v) {
    if (v.size() % 2 != 0) {
      throw std::runtime_error("unaligned memory to word size 2");
    }
    return v | rgv::chunk(2) | rgv::transform([](auto &&c2) {
             uint16_t val{};
             for (uint8_t v : c2) {
               val <<= 8;
               val += v;
             }
             return val;
           });
  }
  void write_verify_eeprom(Firmware const &fw) {}
  void write_verify_userids(Firmware const &fw) {}
  void write_verify_config(Firmware const &fw) {}

  ICSPHeader &icsp;
  ICSPHeader::ExitProg prog_guard;
};

template <typename Map> PICProgrammer(Map, ICSPHeader &) -> PICProgrammer<Map>;
template <typename Map>
PICProgrammer(Map, ICSPHeader &, ICSPHeader::ExitProg) -> PICProgrammer<Map>;