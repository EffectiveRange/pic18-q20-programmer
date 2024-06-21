#include <catch2/catch.hpp>

#include "IntelHex.hpp"
#include "PIC18-Q20.hpp"
#include "Region.hpp"

#include <cstdint>
#include <sstream>

TEST_CASE("int parsing", "[intelhex]") {
  using namespace std::literals;
  using IntelHex::parse_int;
  SECTION("valid input") {
    const auto s = "8000"s;

    SECTION("as base10, unsigned") {
      const auto res = parse_int<uint16_t>(s.begin(), s.end());
      REQUIRE(res == 8000);
    }
    SECTION("as base16, unsigned") {
      const auto res = parse_int<uint16_t>(s.begin(), s.end(), 16);
      REQUIRE(res == 0x8000);
    }
  }
  SECTION("invalid input") {
    SECTION("not a number") {
      const auto s = "80haho"s;
      REQUIRE_THROWS_AS(parse_int<uint16_t>(s.begin(), s.end()),
                        std::system_error);
    }
    SECTION("too big to fit") {
      const auto s = "0x8000"s;
      REQUIRE_THROWS_AS(parse_int<uint8_t>(s.begin(), s.end()),
                        std::system_error);
    }
  }
}

TEST_CASE("Hex line basic parsing", "[intelhex]") {

  std::istringstream iss(":1023A8001551DA22CB0EDE1807E1050EDE18D8A487");

  auto res = IntelHex::parse_hex_line(iss);
  REQUIRE(res);
  REQUIRE(res->len == 16);
  REQUIRE(res->addr == 0x23a8);
  REQUIRE(res->record_type == IntelHex::RecordType::DATA);
  REQUIRE(res->payload.first == 16);
  const auto expected =
      std::array<uint8_t, 16>{0x15, 0x51, 0xDA, 0x22, 0xCB, 0x0E, 0xDE, 0x18,
                              0x07, 0xE1, 0x05, 0x0E, 0xDE, 0x18, 0xD8, 0xA4};
  REQUIRE(std::equal(res->payload.second.begin(),
                     res->payload.second.begin() + res->payload.first,
                     expected.begin(), expected.end()));
}

TEST_CASE("Checksum defect driven test 1", "[intelhex]") {
  std::istringstream iss(":012FE80018D0");
  auto res = IntelHex::parse_hex_line(iss);
  REQUIRE(res);
  REQUIRE(res->len == 1);
  REQUIRE(res->addr == 0x2FE8);
  REQUIRE(res->record_type == IntelHex::RecordType::DATA);
  REQUIRE(res->payload.first == 1);
  REQUIRE(res->payload.second.at(0) == 0x18);
}

TEST_CASE("Hex file basic parsing", "[intelhex]") {

  std::istringstream iss(R"-(:0400000055EF00F0C8
:10000800FC0B3E0B440B4A0BFC0BFC0BFC0BFC0BD8
:10001800FC0BFC0BFC0BFC0BFC0BFC0BFC0BFC0BA0
:012FE80018D0
:102FEA001200120012001100120012001200120048
:042FFA0012001200AF
:020000040030CA
:0B000000ECFFFFFF9FFFFF7FFFFFFFF3
:02001800FFFFE8
:00000001FF
)-");

  auto res = IntelHex::parse_hex_file(pic18fq20, iss);

  REQUIRE(res.size() == 2);
  REQUIRE(res[0].region.name == Address::Region::PROGRAM);

  REQUIRE(res[0].elems[0].base_addr == 0x0);
  REQUIRE(res[0].elems[0].data == byte_vector{0x55, 0xEF, 0x00, 0xF0});

  REQUIRE(res[0].elems[1].base_addr == 0x00000008);
  REQUIRE(res[0].elems[1].data ==
          byte_vector{0xFC, 0x0B, 0x3E, 0x0B, 0x44, 0x0B, 0x4A, 0x0B,
                      0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B,
                      0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B,
                      0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B});

  REQUIRE(res[0].elems[2].base_addr == 0x2FE8);
  REQUIRE(res[0].elems[2].data == byte_vector{0x18});

  REQUIRE(res[0].elems[3].base_addr == 0x2FEA);
  REQUIRE(res[0].elems[3].data == byte_vector{0x12, 0x00, 0x12, 0x00, 0x12,
                                              0x00, 0x11, 0x00, 0x12, 0x00,
                                              0x12, 0x00, 0x12, 0x00, 0x12,
                                              0x00, 0x12, 0x00, 0x12, 0x00});

  REQUIRE(res[1].region.name == Address::Region::CONFIG);
  REQUIRE(res[1].elems.size() == 2);
  REQUIRE(res[1].elems[0].base_addr == 0x00300000);
  REQUIRE(res[1].elems[0].data == byte_vector{0xEC, 0xFF, 0xFF, 0xFF, 0x9F,
                                              0xFF, 0xFF, 0x7F, 0xFF, 0xFF,
                                              0xFF});
  REQUIRE(res[1].elems[1].base_addr == 0x00300018);
  REQUIRE(res[1].elems[1].data == byte_vector{0xFF, 0xFF});
}

TEST_CASE("Ext lin addr chk calc", "[intelhex]") {
  REQUIRE(IntelHex::Dumper::extended_linear_addr_chk(0x30) == 0xCA);
}

TEST_CASE(" data chk calc", "[intelhex]") {
  const auto data = std::array<uint8_t, 16>{
      0x01, 0x01, 0xE6, 0x9D, 0x12, 0x00, 0x01, 0x01,
      0xE6, 0x8B, 0x12, 0x00, 0x05, 0x01, 0xD2, 0x51,
  };
  REQUIRE(IntelHex::Dumper::data_chk(
              0x2FB8, std::span(data.begin(), data.end())) == 0xC4);

  // 0x10+0x2F+0xB8+0x00+0x01+0x01+0xE6+0x9D+0x12+0x00+0x01+0x01+0xE6+0x8B+0x12+0x00+0x05+0x01+0xD2+0x51
}

TEST_CASE("test dump region to intel hex", "[utils][dump_memory]") {
  SECTION("dump CONFIG region") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 11> data{
        0xEC, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF,
    };

    dumper.dump_region(Address::Region::CONFIG,
                       std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":020000040030CA\n"
                        ":0B000000ECFFFFFF9FFFFF7FFFFFFFF3\n");
  }
}

TEST_CASE("test dump line to intel hex", "[utils][dump_memory]") {
  SECTION("dump less than 16 bytes, start address 0") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 4> data{0xEF, 0x55, 0xF0, 0x00};

    dumper.dump_data_line(0, std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":04000000EF55F000C8\n");
  }
  SECTION("dump 16 bytes, start address non-0") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 16> data{
        0x05, 0x8F, 0x12, 0x00, 0x01, 0x01, 0x05, 0x9F,
        0x12, 0x00, 0x00, 0x0E, 0x12, 0x00, 0x01, 0x38,
    };

    dumper.dump_data_line(0x2fd8, std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":102FD800058F12000101059F1200000E1200013832\n");
  }
}

TEST_CASE("test dump memory to intel hex", "[utils][dump_memory]") {

  SECTION("config region") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 11> data{
        0xEC, 0xFF, 0xFF, 0xFF, 0x9F, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF,
    };

    dumper.dump_data_memory(0x300000, std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":020000040030CA\n"
                        ":0B000000ECFFFFFF9FFFFF7FFFFFFFF3\n");
  }
  SECTION("program region") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 32> data{
        0xFC, 0x0B, 0x3E, 0x0B, 0x44, 0x0B, 0x4A, 0x0B, 0xFC, 0x0B, 0xFC,
        0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B,
        0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B, 0xFC, 0x0B,
    };
    dumper.dump_data_memory(8, std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":10000800FC0B3E0B440B4A0BFC0BFC0BFC0BFC0BD8\n"
                        ":10001800FC0BFC0BFC0BFC0BFC0BFC0BFC0BFC0BA0\n");
  }
  SECTION("program region,split line") {
    std::stringstream ss;
    IntelHex::Dumper dumper(ss);
    std::array<uint8_t, 22> data{
        0x08, 0x6F, 0x33, 0xEC, 0x16, 0xF0, 0xEC, 0x0E, 0x06, 0x01, 0x07,
        0x6F, 0x2F, 0x0E, 0x08, 0x6F, 0x24, 0xEC, 0x16, 0xF0, 0x12, 0x00,
    };
    dumper.dump_data_memory(0x2290, std::span(data.begin(), data.end()));
    REQUIRE(ss.str() == ":10229000086F33EC16F0EC0E0601076F2F0E086F77\n"
                        ":0622A00024EC16F0120010\n");
  }
}

TEST_CASE("parse hex file from reference output", "[intelhex]") {
  std::istringstream iss(R"-(:02000004002CCE
:1000000032421161619113540000FFFFFFFFFFFFB7
:10001000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0
:10002000FFFFFFFF6CF5DB01F21614F98602C7141F
:10003000030400080510FFFFFFFFFFFFFFFFFFFFA6
:1000400000000000000000000000000000000000B0
:1000500000000000000000000000000000000000A0
:100060000000000000000000000000000000000090
:100070000000000000000000000000000000000080
:100080000000000000000000000000000000000070
:100090000000000000000000000000000000000060
:1000A0000000000000000000000000000000000050
:1000B0000000000000000000000000000000000040
:1000C0000000000000000000000000000000000030
:1000D0000000000000000000000000000000000020
:1000E0000000000000000000000000000000000010
:1000F0000000000000000000000000000000000000
:00000001FF)-");
  auto res = IntelHex::parse_hex_file(pic18fq20, iss);
  REQUIRE(res.size() == 1);
  REQUIRE(res[0].region.name == Address::Region::DIA);
  REQUIRE(res[0].elems.size() == 1);
  REQUIRE(res[0].elems[0].base_addr == 0x2C0000);
  REQUIRE(res[0].elems[0].data.size() == 256);
  REQUIRE(res[0].elems[0].data.at(0x2F) == 0x14);
}