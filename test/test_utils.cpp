#include "PIC18-Q20.hpp"
#include "Region.hpp"
#include <catch2/catch.hpp>

#include <cstdint>
#include <iostream>
#include <utils.hpp>

#include <sstream>

TEST_CASE("test dump hex line", "[utils][dump_line]") {

  SECTION("full line, word size = 2, line width = 4") {
    std::stringstream ss;
    OstreamDumper dumper(ss, 4 * 2);
    dumper.dump_line(0x3f, std::array<uint8_t, 8>{0x48, 0x03, 0x6c, 0x6c, 0x00,
                                                  0x20, 0x57, 0x6f});
    REQUIRE(ss.str() == "0x00003f | 48 03 6c 6c 00 20 57 6f | H.ll. Wo |");
  }
  SECTION("full line, word size = 2, line width = 8") {
    std::stringstream ss;
    OstreamDumper dumper(ss, 7 * 2);
    dumper.dump_line(0x3f, std::array<uint8_t, 16>{
                               0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57, 0x6f,
                               0x72, 0x6c, 0x64, 0x21, 0x21, 0x21, 0x00, 0x01});
    REQUIRE(ss.str() ==
            "0x00003f | 48 03 6c 6c 00 20 57 6f 72 6c 64 21 21 21 00 01 | "
            "H.ll. World!!!.. |");
  }
  SECTION("partial line, word size = 2, line width = 16") {
    std::stringstream ss;
    OstreamDumper dumper(ss, 8 * 2);
    dumper.dump_line(0x3f, std::array<uint8_t, 8>{0x48, 0x03, 0x6c, 0x6c, 0x00,
                                                  0x20, 0x57, 0x00});
    REQUIRE(ss.str() ==
            "0x00003f | 48 03 6c 6c 00 20 57 00                         | "
            "H.ll. W.         |");
  }
  SECTION("partial line, word size = 1, line width = 8") {
    std::stringstream ss;
    OstreamDumper dumper(ss, 8);

    dumper.dump_line(
        0x3f, std::array<uint8_t, 6>{0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20});
    REQUIRE(ss.str() == "0x00003f | 48 03 6c 6c 00 20       | "
                        "H.ll.    |");
  }
  SECTION("full line, word size = 1, line width = 8") {
    std::stringstream ss;
    OstreamDumper dumper(ss, 8);
    dumper.dump_line(0x3f, std::array<uint8_t, 8>{0x48, 0x03, 0x6c, 0x6c, 0x00,
                                                  0x20, 0x57, 0x6f});
    REQUIRE(ss.str() == "0x00003f | 48 03 6c 6c 00 20 57 6f | "
                        "H.ll. Wo |");
  }
}

TEST_CASE("test dump memory", "[utils][dump_memory]") {
  SECTION("dump multiline memory, 8 bytes per line, wordsize=1") {

    std::stringstream ss;
    OstreamDumper dumper(ss, 8);
    dumper.dump_memory(0xa0, std::array<uint8_t, 15>{
                                 0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57, 0x6f,
                                 0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57});
    REQUIRE(ss.str() == "0x0000a0 | 48 03 6c 6c 00 20 57 6f | H.ll. Wo |\n"
                        "0x0000a8 | 48 03 6c 6c 00 20 57    | H.ll. W  |\n");
  }

  SECTION("dump multiline memory, 8 bytes per line, wordsize=2") {

    std::stringstream ss;
    OstreamDumper dumper(ss, 8);

    dumper.dump_memory(0xa0, std::array<uint8_t, 14>{
                                 0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57, 0x6f,
                                 0x72, 0x6c, 0x64, 0x21, 0x21, 0x21});
    REQUIRE(ss.str() == "0x0000a0 | 48 03 6c 6c 00 20 57 6f | H.ll. Wo |\n"
                        "0x0000a8 | 72 6c 64 21 21 21       | rld!!!   |\n");
  }
}

TEST_CASE("test dump region", "[utils][dump_memory]") {
  SECTION("dump EEPROM region") {
    std::stringstream ss;
    OstreamDumper dumper(ss);
    std::array<uint8_t, 15> data{0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57, 0x6f,
                                 0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57};
    dumper.dump_region(Address::Region::EEPROM,
                       std::span(data.begin(), data.end()));
    REQUIRE(ss.str() ==
            "Region name:EEPROM address:[380000h,380100h)  word size: 1\n"
            "0x380000 | 48 03 6c 6c 00 20 57 6f 48 03 6c 6c 00 20 57    | "
            "H.ll. WoH.ll. W  |\n");
  }
  SECTION("dump PROGRAM region") {
    std::stringstream ss;
    OstreamDumper dumper(ss);
    std::array<uint8_t, 14> data{0x48, 0x03, 0x6c, 0x6c, 0x00, 0x20, 0x57,
                                 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x21, 0x21};
    dumper.dump_region(Address::Region::PROGRAM,
                       std::span(data.begin(), data.end()));
    REQUIRE(
        ss.str() ==
        "Region name:PROGRAM address:[000000h,010000h)  word size: 2\n"
        "0x000000 | 48 03 6c 6c 00 20 57 6f 72 6c 64 21 21 21       | H.ll. "
        "World!!!   |\n");
  }
}

TEST_CASE("test regions", "[utils][region]") {
  const auto expected = 256;
  REQUIRE(pic18q20map::dia_region_v.size() == expected);
  Address::region_data<pic18q20map::dia_region_v> d{};
  REQUIRE(d.data.size() == expected);
}

TEST_CASE("detailed parse", "[utils][parse]") {

  std::array<uint8_t, 6> data{0, 1, 2, 3, 4, 5};
  std::span<uint8_t const, 6> dp(data);
  const auto res = std::make_tuple(detail::parse(dp.subspan<0, 2>()),
                                   detail::parse(dp.subspan<2, 4>()));
}