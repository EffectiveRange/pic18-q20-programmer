// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#include "prog_utils.hpp"

#include <fstream>

#include <argparse/argparse.hpp>

#include "ICSP_pins.hpp"
#include "PICProgrammer.hpp"
#include <ICSP_header.hpp>
#include <IGPIO.hpp>
#include <IntelHex.hpp>
#include <PIC18-Q20.hpp>
#include <Region.hpp>
#include <memory>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <utils.hpp>

void print_device_info(std::ostream &os, DeviceId const &id, DCI const &dci,
                       DIA const &dia) {
  os << fmt::format("Device Id: 0x{:04x} ({})\n"
                    "Revision Id: 0x{:04x} ({})\n",
                    id.deviceId, id.deviceIdStr(), id.revisionId,
                    id.revisionStr());

  os << fmt::format("Device Configuration Information:\n"
                    "  Erase page size: {} words\n"
                    "  No. of erasable pages: {} pages\n"
                    "  EEPROM size: {} bytes\n"
                    "  Pin count: {} pins\n",
                    dci.erase_page_size, dci.num_erasable_pages,
                    dci.eeprom_size, dci.pin_cnt);

  os << fmt::format("Device Information Area:\n"
                    "  Microchip UID: {}\n"
                    "  Optional Ext. UID: {}\n",
                    format_uid(dia.mchp_uid), format_uid(dia.ext_uid));

  auto format_coeffs = [](std::string_view key, TempCoeffs const &v) {
    return fmt::format("  Temperature Sensor Parameters({}):\n"
                       "    Gain: 0x{:04x} ({:.6f} C_deg)\n"
                       "    ADC 90 deg. reading: 0x{:04x}\n"
                       "    Offset: 0x{:04x}\n",
                       key, v.gain, v.gain_val(), v.adc_90, v.offset);
  };
  os << format_coeffs("low range", dia.low_temp_coeffs);
  os << format_coeffs("high range", dia.high_temp_coeffs);
  os << fmt::format("Fixed Voltage Reference Data:\n"
                    "  ADC FVR1 Output Voltage 1X: 0x{0:04x} ({0} mV)\n"
                    "  ADC FVR1 Output Voltage 2X: 0x{1:04x} ({1} mV)\n"
                    "  ADC FVR1 Output Voltage 4X: 0x{2:04x} ({2} mV)\n"
                    "  Comparator FVR2 Output Voltage 1X: 0x{3:04x} ({3} mV)\n"
                    "  Comparator FVR2 Output Voltage 2X: 0x{4:04x} ({4} mV)\n"
                    "  Comparator FVR2 Output Voltage 4X: 0x{5:04x} ({5} mV)\n",
                    dia.fixed_voltage_ref[0], dia.fixed_voltage_ref[1],
                    dia.fixed_voltage_ref[2], dia.fixed_voltage_comp[0],
                    dia.fixed_voltage_comp[1], dia.fixed_voltage_comp[2]);
}

std::unique_ptr<AugmentedParser> get_parser() {
  std::unique_ptr<AugmentedParser> parser(new AugmentedParser{

      argparse::ArgumentParser{"picprogrammer", PICPROG_VER,
                               argparse::default_arguments::all},
      0});

  auto *program = &parser->parser;
  program->add_argument("--headers")
      .help("dumps static section header information, then exits")
      .flag();
  program->add_argument("-q", "--quiet")
      .help("quiet mode, don't print anything to stdout/stderr when "
            "reading/dumping")
      .flag();
  // Exec section
  auto &exec_group = program->add_mutually_exclusive_group();

  exec_group.add_argument("-i", "--info")
      .help("dump high level device information on either the FW file if "
            "`--file` is specified or otherwise from the device, then exits")
      .flag();

  exec_group.add_argument("-d", "--dump")
      .help("dump section memory form either the device or the input firmware "
            "file then exits")
      .flag();

  exec_group.add_argument("-w", "--write")
      .help("write the firmware into the device")
      .flag();

  auto &address_group = program->add_mutually_exclusive_group();

  address_group.add_argument("-a", "--address")
      .help("base address (either in decimal or hexadecimal format) ");

  address_group.add_argument("-f", "--file")
      .help("input/output firmware file (either in Intel Hex of ELF format)");

  program->add_argument("-c", "--content")
      .help("Content to write, either a hex "
            "string,e.g 0xAABB..., or an ASCII string. Base address or exactly "
            "one section must be specified, if content is not integral "
            "multiple of word size, it will be padded with 0xFF bytes");

  program->add_argument("-e", "--erase")
      .default_value<std::vector<std::string>>({})
      .append()
      .help(
          "list of section names to bulk erase (on top of programmed regions)");

  program->add_argument("-s", "--section")
      .default_value<std::vector<std::string>>({})
      .append()
      .help("list of section names to operate on, if missing all sections are "
            "considered");

  program->add_argument("-V", "--verbose")
      .action([verbose = std::addressof(parser->verbosity)](const auto &) {
        *verbose += 1;
      })
      .append()
      .nargs(0)
      .help("print more information about the operation")
      .default_value(false)
      .implicit_value(true);

  program->add_argument("--gpio-clk")
      .help("GPIO pin number to be used for the ICSP CLK line")
      .required()
      .default_value(ICSPPins{}.clk_pin)
      .scan<'i', unsigned>();

  program->add_argument("--gpio-data")
      .help("GPIO pin number to be used for the ICSP DATA line")
      .required()
      .default_value(ICSPPins{}.data_pin)
      .scan<'i', unsigned>();

  program->add_argument("--gpio-mclr")
      .help("GPIO pin number to be used for the ICSP MCLR line")
      .required()
      .default_value(ICSPPins{}.mclr_pin)
      .scan<'i', unsigned>();

  auto &prog_en_group = program->add_mutually_exclusive_group();

  prog_en_group.add_argument("--gpio-prog-en")
      .help("GPIO pin number to be used for the PROG EN line (EXT/INT ICSP "
            "header)")
      .required()
      .default_value(ICSPPins{}.prog_en_pin.value())
      .scan<'i', unsigned>();

  prog_en_group.add_argument("--no-gpio-prog-en")
      .help("Don't use PROG EN signal")
      .flag()
      .default_value(false);

  auto &format_group = program->add_mutually_exclusive_group();

  format_group.add_argument("--hex").flag().help(
      "use intel hex format for the firmware data, or the supplied data string "
      "is in hex format");
  format_group.add_argument("--elf").flag().help(
      "use elf format for the firmware data");

  format_group.add_argument("-b", "--binary")
      .flag()
      .help("display numbers in binary format instead of hexadecimal");
  return std::move(parser);
}

void dump_sections(IDumper &dumper, ICSPHeader &icsp,
                   std::vector<std::string> const &sections) {
  auto prog = icsp.enter_programming();
  if (sections.empty()) {
    dump_regions(dumper, prog, pic18fq20);
    return;
  }
  // TODO: sort section names based on start address
  dumper.dump_start();
  for (const auto &name : sections) {
    Address::with_region(
        name, [&](auto idx, auto region) { dump_region(dumper, prog, region); },
        pic18fq20);
  }
  dumper.dump_end();
}

std::pair<fs::path, Firmware>
process_input_file(argparse::ArgumentParser &parser) {
  if (parser["--hex"] == true) {
    fs::path inputfile = (parser.get<std::string>("-f"));

    if (const auto exists = fs::exists(inputfile);
        !exists || !fs::is_regular_file(inputfile)) {
      throw fs::filesystem_error(
          "Input firmware file non-existent or not a file",
          std::make_error_code(!exists ? std::errc::no_such_file_or_directory
                                       : std::errc::is_a_directory));
    }
    std::ifstream ifs(inputfile);
    return {inputfile, IntelHex::parse_hex_file(pic18fq20, ifs)};
  }
  throw std::runtime_error("Input file format not supported yet.");
}
void print_fwfile_info(fs::path p, Firmware const &fw) {
  std::cout << fmt::format("Info from firmware file : {}\n", p.string());
  std::cout << fmt::format("  Number of regions: {}\n", fw.size());
  for (auto &r : fw) {
    const auto start = r.region.start;
    const auto end = r.region.end;
    auto datasizes =
        r.elems | rgv::transform([](const auto &re) { return re.data.size(); });
    const auto total_size =
        std::accumulate(rg::begin(datasizes), rg::end(datasizes), size_t{0});
    const auto info = fmt::format("  Region: {} [{:06x}-{:06x})\n"
                                  "    Contiguous sections:{}\n"
                                  "    Total size in bytes:{}\n",
                                  r.region.name_str(), start, end,
                                  r.elems.size(), total_size);
    std::cout << info;
  }
}
FWFileDescr get_fw_file(argparse::ArgumentParser &parser) {
  if (parser.present("-f")) {
    return process_input_file(parser);
  }
  return std::nullopt;
}
Address::Region extra_erease_regions(argparse::ArgumentParser const &parser) {
  auto extra_erase = parser.get<std::vector<std::string>>("--erase");
  const auto extra = rg::accumulate(extra_erase, Address::Region::INVALID,
                                    std::bit_or<>{}, Address::string_to_region);
  return extra;
}

void emitInfo(FWFileDescr const &fw, ICSPPins const &pins) {
  if (fw) {
    const auto &[path, fwdata] = *fw;
    print_fwfile_info(path, fwdata);
  } else {
    auto icsp = ICSPHeader(IGPIO::Create(), pins);
    PICProgrammer programmer(pic18fq20, icsp, icsp.enter_programming());
    const auto devid = programmer.read_device_id();
    const auto dci = programmer.read_dci();
    const auto dia = programmer.read_dia();
    print_device_info(std::cout, devid, dci, dia);
  }
}
void execWrite(argparse::ArgumentParser const &args, FWFileDescr const &fw,
               Address::Region extra_erease, ICSPPins const &pins) {
  auto icsp = ICSPHeader(IGPIO::Create(), pins);
  auto programmer = PICProgrammer{pic18fq20, icsp};
  programmer.program_verify(fw.value().second, extra_erease);
}

void execDump(argparse::ArgumentParser const &args, FWFileDescr const &fw,
              ICSPPins const &pins) {
  auto icsp = ICSPHeader(IGPIO::Create(), pins);
  // TODO: use fw file if specified
  const auto hexformat = args["hex"] == true;
  const auto elfformat = args["elf"] == true;
  const auto tofile = !!fw;
  const auto quiet = args["quiet"] == true;
  const auto binformat = args["binary"] == true;
  const auto &sections = args.get<std::vector<std::string>>("--section");
  if (quiet && !tofile) {
    throw std::logic_error("quiet mode with no output file");
  }
  if (args["hex"] == true) {
    IntelHex::Dumper dumper(std::cout);
    dump_sections(dumper, icsp, sections);
  } else if (!elfformat) {
    OstreamDumper dumper(std::cout);
    dump_sections(dumper, icsp, sections);
  } else {
    throw std::runtime_error("Dump format not implemented");
  }
}

void execErase(const Address::Region &extra_erease, ICSPPins const &pins) {
  auto icsp = ICSPHeader(IGPIO::Create(), pins);
  icsp.bulk_erase(extra_erease);
}