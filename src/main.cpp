// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos
// <attila.gombos@effective-range.com> SPDX-License-Identifier: MIT

#include <iostream>
#include <ostream>

#include <er/hwinfo.hpp>

#include "prog_utils.hpp"

#include <PIC18-Q20.hpp>

#include <argparse/argparse.hpp>

int main(int argc, char *argv[]) try {
  auto pparser = get_parser();
  auto &aug_parser = *pparser;
  auto &parser = aug_parser.parser;
  parser.parse_args(argc, argv);
  const auto verbose = verbosity(aug_parser.verbosity);

  if (parser["--headers"] == true) {
    std::cout << "Section information for PIC18F-Q20:\n";
    print_headers(std::cout, pic18fq20);
    return 0;
  }
  const auto hwdb_path =
      std::filesystem::path(parser.get<std::string>("--hwdb-path"));
  const auto device_tree_path =
      std::filesystem::path(parser.get<std::string>("--device-tree"));
  const auto info =
      er::hwinfo::get(device_tree_path, hwdb_path / "hwdb.json",
                      hwdb_path / "hwdb-schema.json"); // initialize hwinfo
  auto fw = get_fw_file(parser);
  const auto extra_erease = extra_erease_regions(parser);
  const auto pins = icsp_pins(info, parser);

  if (parser["--info"] == true) {
    emitInfo(fw, pins);
    return 0;
  } else if (parser["--dump"] == true) {
    execDump(parser, fw, pins);
    return 0;
  } else if (parser["--write"] == true) {
    execWrite(parser, fw, extra_erease, pins);
    return 0;
  }

  if (extra_erease != Address::Region::INVALID) {
    execErase(extra_erease, pins);
    return 0;
  }

  return 0;
} catch (IGPIO::Interrupted const &e) {
  std::cerr << e.what() << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << "ERROR:" << e.what() << '\n';
  return -1;
} catch (...) {
  std::cerr << "ERROR: Unknown exception occurred...\n";
  return -2;
}
