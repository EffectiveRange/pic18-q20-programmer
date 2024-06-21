// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ICSP_pins.hpp"
#include "Region.hpp"
#include "argparse/argparse.hpp"
#include <PICProgrammer.hpp>

#include <argparse/argparse.hpp>
#include <concepts>
#include <fmt/format.h>

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>

enum class Verbosity { ERROR = 0, INFO = 1, DEBUG = 2, MAX = DEBUG };

template <std::integral T> Verbosity verbosity(T val) {
  return static_cast<Verbosity>(
      std::clamp(val, 0, static_cast<T>(Verbosity::MAX)));
}

inline auto icsp_pins(argparse::ArgumentParser const &parser) {
  ICSPPins pins{};
  pins.clk_pin = parser.get<unsigned>("--gpio-clk");
  pins.mclr_pin = parser.get<unsigned>("--gpio-mclr");
  pins.data_pin = parser.get<unsigned>("--gpio-data");
  if (parser["--no-gpio-prog-en"] == true) {
    pins.prog_en_pin = std::nullopt;
  } else {
    pins.prog_en_pin = parser.get<unsigned>("--gpio-prog-en");
  }
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