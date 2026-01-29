// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos
// <attila.gombos@effective-range.com> SPDX-License-Identifier: MIT

#pragma once

#include "ICSP_pins.hpp"
#include "Region.hpp"
#include "argparse/argparse.hpp"
#include <PICProgrammer.hpp>

#include <argparse/argparse.hpp>
#include <concepts>
#include <fmt/format.h>

#include <er/hwinfo.hpp>

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>

enum class Verbosity { ERROR = 0, INFO = 1, DEBUG = 2, MAX = DEBUG };

template <std::integral T> Verbosity verbosity(T val) {
  return static_cast<Verbosity>(
      std::clamp(val, 0, static_cast<T>(Verbosity::MAX)));
}

namespace icsp_pin_names {
inline constexpr auto &CLK = "ICSP_CLK";
inline constexpr auto &DATA = "ICSP_DATA";
inline constexpr auto &MCLR = "ICSP_MCLR";
inline constexpr auto &PROG_EN = "ICSP_PROG_EN";
} // namespace icsp_pin_names

/// @brief Get ICSP pin from hwinfo by name
/// @return pin number if found, std::nullopt otherwise
inline std::optional<unsigned> get_hwinfo_pin(er::hwinfo::info const &info,
                                              std::string_view pin_name) {
  auto it = info.pins.find(pin_name);
  if (it != info.pins.end()) {
    return static_cast<unsigned>(it->number);
  }
  return std::nullopt;
}

// Resolve pin: CLI override > hwinfo > throw
inline unsigned resolve_pin(std::optional<er::hwinfo::info> const &info,
                            argparse::ArgumentParser const &parser,
                            std::string_view cli_arg,
                            std::string_view hwinfo_name) {
  if (auto cli_val = parser.present<unsigned>(cli_arg)) {
    return *cli_val;
  }
  if (info) {
    if (auto hw_val = get_hwinfo_pin(*info, hwinfo_name)) {
      return *hw_val;
    }
  }
  throw std::runtime_error(fmt::format(
      "Missing required ICSP pin: {} (or {} in hwdb)", cli_arg, hwinfo_name));
};

/// @brief Build ICSPPins from hwinfo with optional CLI overrides
/// @param info Hardware info (may be nullopt if device tree not available)
/// @param parser Argument parser for CLI overrides
/// @return ICSPPins with all values populated from hwinfo and/or CLI
/// @throws std::runtime_error if required pins are missing
inline auto icsp_pins(std::optional<er::hwinfo::info> const &info,
                      argparse::ArgumentParser const &parser) {

  ICSPPins pins{};
  pins.clk_pin = resolve_pin(info, parser, "--gpio-clk", icsp_pin_names::CLK);
  pins.data_pin =
      resolve_pin(info, parser, "--gpio-data", icsp_pin_names::DATA);
  pins.mclr_pin =
      resolve_pin(info, parser, "--gpio-mclr", icsp_pin_names::MCLR);
  pins.prog_en_pin =
      parser["--no-gpio-prog-en"] == true
          ? std::nullopt
          : std::make_optional(resolve_pin(info, parser, "--gpio-prog-en",
                                           icsp_pin_names::PROG_EN));
  return pins;
}

namespace fs = std::filesystem;
using FWFileDescr = std::optional<std::pair<fs::path, Firmware>>;

struct AugmentedParser {
  argparse::ArgumentParser parser;
  int verbosity = 0;
};

void print_device_info(std::ostream &os, DeviceId const &id, DCI const &dci,
                       DIA const &dia);

std::unique_ptr<AugmentedParser> get_parser();

template <auto... R>
void print_headers(std::ostream &os, Address::RegionMap<R...> const &) {
  ((os << R << '\n'), ...);
}

template <auto R>
void dump_region(IDumper &dumper, ICSPHeader::ExitProg &prog,
                 Address::region_t<R> region) {
  auto result = prog.icsp().read_region(region);
  dumper.dump_region(result.region().name, std::span{result.data});
}

template <auto... R>
void dump_regions(IDumper &dumper, ICSPHeader::ExitProg &prog,
                  Address::RegionMap<R...> const &) {
  dumper.dump_start();
  (dump_region(dumper, prog, Address::region_t<R>{}), ...);
  dumper.dump_end();
}

void dump_sections(std::ostream &os, ICSPHeader &icsp,
                   std::vector<std::string> const &sections);

std::pair<fs::path, Firmware>
process_input_file(argparse::ArgumentParser &parser);

FWFileDescr get_fw_file(argparse::ArgumentParser &parser);

void print_fwfile_info(fs::path p, Firmware const &fw);

template <typename Rng> auto format_uid(Rng &&rng) {
  std::string buff;
  bool first = true;
  for (const auto val : rng) {
    fmt::format_to(std::back_inserter(buff), "{}{:04x}", (first ? "" : ":"),
                   val);
    first = false;
  }
  return buff;
}

Address::Region extra_erease_regions(argparse::ArgumentParser const &parser);

void emitInfo(FWFileDescr const &fw, ICSPPins const &);

void execWrite(argparse::ArgumentParser const &, FWFileDescr const &fw,
               Address::Region extra_erease, ICSPPins const &);

void execDump(argparse::ArgumentParser const &, FWFileDescr const &fw,
              ICSPPins const &);

void execErase(const Address::Region &extra_erease, ICSPPins const &);