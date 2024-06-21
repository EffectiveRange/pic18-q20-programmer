#include "ICSP_header.hpp"
#include "IGPIO.hpp"
#include "MockGPIO.hpp"
#include "MockPIC18Q20.hpp"
#include "PIC18-Q20.hpp"
#include "Region.hpp"
#include <catch2/catch.hpp>
#include <csignal>
#include <cstdint>

#include "test_utils.hpp"

#include <signal.h>

TEST_CASE("test", "[test]") { REQUIRE(1 == 1); }

TEST_CASE("Enter LVP mode", "[ICSP]") {
  auto [gpio, pic] = setup();
  auto icsp = ICSPHeader(gpio);
  REQUIRE(in_state<IDLE>(pic->state()));
  {
    auto prog = icsp.enter_programming();
    REQUIRE(in_state<PROGRAMMING>(pic->state()));
  }
  REQUIRE(in_state<IDLE>(pic->state()));
}

TEST_CASE("Cleanup in case of SIGINT/SIGTERM", "[ICSP]") {
  TestObjects pobj{nullptr};
  {
    auto objs = setup();
    pobj = objs;
    auto &[gpio, pic] = objs;
    auto icsp = ICSPHeader(gpio);

    REQUIRE(in_state<IDLE>(pic->state()));

    auto prog = icsp.enter_programming();
    REQUIRE(in_state<PROGRAMMING>(pic->state()));
    raise(SIGTERM);
    REQUIRE_THROWS(gpio->gpio_write(ICSPPins{}.data_pin, 1));
  }
  REQUIRE(in_state<IDLE>(pobj.pic->state()));

  const auto [port, mode, val, listener] =
      pobj.gpio->get_state(ICSPPins{}.prog_en_pin.value()).value();
  REQUIRE(port == ICSPPins{}.prog_en_pin.value());
  REQUIRE(mode == IGPIO::Modes::OUTPUT);
  REQUIRE(val == 0);
}

TEST_CASE("Reading device IDs", "[ICSP]") {

  auto objs = setup();
  objs.pic->buffer()[0x3FFFFC] = 0xDE;
  objs.pic->buffer()[0x3FFFFD] = 0xAD;
  objs.pic->buffer()[0x3FFFFE] = 0xBE;
  objs.pic->buffer()[0x3FFFFF] = 0xEF;
  auto icsp = ICSPHeader(objs.gpio);
  auto prog = icsp.enter_programming();
  const auto result = icsp.read_region(pic18q20map::id_region);
  REQUIRE(result.region().word_size == 2);
  REQUIRE(result.region().word_cnt() == 2);
  REQUIRE(result.region().start == 0x3FFFFC);
  REQUIRE(result.region().end == 0x400000);
  REQUIRE(result.data[0] == 0xDE);
  REQUIRE(result.data[1] == 0xAD);
  REQUIRE(result.data[2] == 0xBE);
  REQUIRE(result.data[3] == 0xEF);
}

TEST_CASE("Writing EEPRPOM", "[ICSP]") {

  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  auto prog = icsp.enter_programming();
  std::vector<uint8_t> data{0xde, 0xad, 0xbe, 0xef};
  const auto result =
      icsp.write(pic18fq20, pic18q20map::eeprom_region.value.start,
                 data.begin(), data.end());
  const auto start = pic18q20map::eeprom_region.value.start;
  // This would toggle out of bounds write in the mocked PIC
  // REQUIRE(objs.pic->buffer()[start - 1] == 0xff);
  REQUIRE(objs.pic->buffer()[start] == 0xde);
  REQUIRE(objs.pic->buffer()[start + 1] == 0xad);
  REQUIRE(objs.pic->buffer()[start + 2] == 0xbe);
  REQUIRE(objs.pic->buffer()[start + 3] == 0xef);
  REQUIRE(objs.pic->buffer()[start + 4] == 0xff);
}

TEST_CASE("Writing Program", "[ICSP]") {

  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  auto prog = icsp.enter_programming();
  std::vector<uint8_t> data{0xF0, 0x0B, 0x50, 0x27, 0xB4, 0xD8, 0xEF, 0xC7,
                            0xF0, 0x0A, 0xEF, 0xC9, 0xF0, 0x0A, 0xEF, 0xE5};

  const auto result = icsp.write(pic18fq20, 0x1580, data.begin(), data.end());
  const auto start = pic18q20map::eeprom_region.value.start;
  REQUIRE(objs.pic->buffer()[0x157E] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x157F] == 0xFF);

  REQUIRE(objs.pic->buffer()[0x1580] == 0xF0);
  REQUIRE(objs.pic->buffer()[0x1581] == 0x0B);
  REQUIRE(objs.pic->buffer()[0x1582] == 0x50);
  REQUIRE(objs.pic->buffer()[0x1583] == 0x27);
  REQUIRE(objs.pic->buffer()[0x1584] == 0xB4);
  REQUIRE(objs.pic->buffer()[0x1585] == 0xD8);
  REQUIRE(objs.pic->buffer()[0x1586] == 0xEF);
  REQUIRE(objs.pic->buffer()[0x1587] == 0xC7);
  REQUIRE(objs.pic->buffer()[0x1588] == 0xF0);
  REQUIRE(objs.pic->buffer()[0x1589] == 0x0A);
  REQUIRE(objs.pic->buffer()[0x158A] == 0xEF);
  REQUIRE(objs.pic->buffer()[0x158B] == 0xC9);
  REQUIRE(objs.pic->buffer()[0x158C] == 0xF0);
  REQUIRE(objs.pic->buffer()[0x158D] == 0x0A);
  REQUIRE(objs.pic->buffer()[0x158e] == 0xEF);
  REQUIRE(objs.pic->buffer()[0x158F] == 0xE5);

  REQUIRE(objs.pic->buffer()[0x1590] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x1591] == 0xFF);
}

TEST_CASE("Writing Config", "[ICSP]") {

  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  auto prog = icsp.enter_programming();
  std::vector<uint8_t> data1{
      0xEC, 0x01, 0x02, 0x03, 0x9F, 0x40, 0x50, 0x7F, 0x66, 0x77, 0x88,
  };
  std::vector<uint8_t> data2{
      0xDE,
      0xAD,
  };
  const auto result =
      icsp.write(pic18fq20, 0x00300000, data1.begin(), data1.end());
  const auto result2 =
      icsp.write(pic18fq20, 0x00300018, data2.begin(), data2.end());
  // This would toggle out of bounds write in the mocked PIC
  // REQUIRE(objs.pic->buffer()[start - 1] == 0xff);
  REQUIRE(objs.pic->buffer()[0x00300000] == 0xEC);
  REQUIRE(objs.pic->buffer()[0x00300001] == 0x01);
  REQUIRE(objs.pic->buffer()[0x00300002] == 0x02);
  REQUIRE(objs.pic->buffer()[0x00300003] == 0x03);
  REQUIRE(objs.pic->buffer()[0x00300004] == 0x9F);
  REQUIRE(objs.pic->buffer()[0x00300005] == 0x40);
  REQUIRE(objs.pic->buffer()[0x00300006] == 0x50);
  REQUIRE(objs.pic->buffer()[0x00300007] == 0x7F);
  REQUIRE(objs.pic->buffer()[0x00300008] == 0x66);
  REQUIRE(objs.pic->buffer()[0x00300009] == 0x77);
  REQUIRE(objs.pic->buffer()[0x0030000A] == 0x88);
  REQUIRE(objs.pic->buffer()[0x0030000B] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x0030000C] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x0030000D] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x0030000E] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x0030000F] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300010] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300011] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300012] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300013] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300014] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300015] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300016] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300017] == 0xFF);
  REQUIRE(objs.pic->buffer()[0x00300018] == 0xDE);
  REQUIRE(objs.pic->buffer()[0x00300019] == 0xAD);
}

TEST_CASE("Bulk Erase", "[ICSP]") {

  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  // Setup some code
  objs.pic->buffer()[10] = 0xDE;
  objs.pic->buffer()[11] = 0xAD;
  objs.pic->buffer()[12] = 0xBE;
  objs.pic->buffer()[13] = 0xEF;
  // Setup some user ids
  objs.pic->buffer()[0x00200000] = 0xAB;
  objs.pic->buffer()[0x00200001] = 0xCD;
  objs.pic->buffer()[0x00200002] = 0x01;
  objs.pic->buffer()[0x00200003] = 0x02;
  // Setup some EEPROM data
  objs.pic->buffer()[0x00380000] = 0x01;
  objs.pic->buffer()[0x00380001] = 0x02;
  objs.pic->buffer()[0x00380002] = 0x03;
  objs.pic->buffer()[0x00380003] = 0x04;
  // setup some config
  objs.pic->buffer()[0x00300000] = 0xEC;
  objs.pic->buffer()[0x00300001] = 0x01;
  objs.pic->buffer()[0x00300002] = 0x02;
  objs.pic->buffer()[0x00300003] = 0x03;
  auto prog = icsp.enter_programming();
  std::array<uint8_t, 4> buff;
  icsp.read_n(pic18fq20, 10, buff.begin(), 4);
  REQUIRE(buff[0] == 0xDE);
  REQUIRE(buff[1] == 0xAD);
  REQUIRE(buff[2] == 0xBE);
  REQUIRE(buff[3] == 0xEF);
  SECTION("erase prog") {
    icsp.bulk_erase(Address::Region::PROGRAM);
    icsp.read_n(pic18fq20, 10, buff.begin(), 4);
    REQUIRE(buff[0] == 0xFF);
    REQUIRE(buff[1] == 0xFF);
    REQUIRE(buff[2] == 0xFF);
    REQUIRE(buff[3] == 0xFF);
    // PROGRAM
    REQUIRE(objs.pic->buffer()[10] == 0xFF);
    REQUIRE(objs.pic->buffer()[11] == 0xFF);
    REQUIRE(objs.pic->buffer()[12] == 0xFF);
    REQUIRE(objs.pic->buffer()[13] == 0xFF);
    // USER
    REQUIRE(objs.pic->buffer()[0x00200000] == 0xAB);
    REQUIRE(objs.pic->buffer()[0x00200001] == 0xCD);
    REQUIRE(objs.pic->buffer()[0x00200002] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00200003] == 0x02);
    // EEPROM
    REQUIRE(objs.pic->buffer()[0x00380000] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00380001] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00380002] == 0x03);
    REQUIRE(objs.pic->buffer()[0x00380003] == 0x04);
    // CONFIG
    REQUIRE(objs.pic->buffer()[0x00300000] == 0xEC);
    REQUIRE(objs.pic->buffer()[0x00300001] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00300002] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00300003] == 0x03);
  }
  SECTION("erase prog and config") {
    icsp.bulk_erase(Address::Region::PROGRAM | Address::Region::CONFIG);
    icsp.read_n(pic18fq20, 10, buff.begin(), 4);
    REQUIRE(buff[0] == 0xFF);
    REQUIRE(buff[1] == 0xFF);
    REQUIRE(buff[2] == 0xFF);
    REQUIRE(buff[3] == 0xFF);
    // PROGRAM
    REQUIRE(objs.pic->buffer()[10] == 0xFF);
    REQUIRE(objs.pic->buffer()[11] == 0xFF);
    REQUIRE(objs.pic->buffer()[12] == 0xFF);
    REQUIRE(objs.pic->buffer()[13] == 0xFF);
    // USER
    REQUIRE(objs.pic->buffer()[0x00200000] == 0xAB);
    REQUIRE(objs.pic->buffer()[0x00200001] == 0xCD);
    REQUIRE(objs.pic->buffer()[0x00200002] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00200003] == 0x02);
    // EEPROM
    REQUIRE(objs.pic->buffer()[0x00380000] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00380001] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00380002] == 0x03);
    REQUIRE(objs.pic->buffer()[0x00380003] == 0x04);
    // CONFIG
    REQUIRE(objs.pic->buffer()[0x00300000] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300001] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300002] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300003] == 0xFF);
  }
  SECTION("erase EEPROM") {
    icsp.bulk_erase(Address::Region::EEPROM);
    icsp.read_n(pic18fq20, 10, buff.begin(), 4);
    REQUIRE(buff[0] == 0xDE);
    REQUIRE(buff[1] == 0xAD);
    REQUIRE(buff[2] == 0xBE);
    REQUIRE(buff[3] == 0xEF);
    // PROGRAM
    REQUIRE(objs.pic->buffer()[10] == 0xDE);
    REQUIRE(objs.pic->buffer()[11] == 0xAD);
    REQUIRE(objs.pic->buffer()[12] == 0xBE);
    REQUIRE(objs.pic->buffer()[13] == 0xEF);
    // USER
    REQUIRE(objs.pic->buffer()[0x00200000] == 0xAB);
    REQUIRE(objs.pic->buffer()[0x00200001] == 0xCD);
    REQUIRE(objs.pic->buffer()[0x00200002] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00200003] == 0x02);
    // EEPROM
    REQUIRE(objs.pic->buffer()[0x00380000] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380001] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380002] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380003] == 0xFF);
    // CONFIG
    REQUIRE(objs.pic->buffer()[0x00300000] == 0xEC);
    REQUIRE(objs.pic->buffer()[0x00300001] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00300002] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00300003] == 0x03);
  }
  SECTION("erase USER") {
    icsp.bulk_erase(Address::Region::USER);
    icsp.read_n(pic18fq20, 10, buff.begin(), 4);
    REQUIRE(buff[0] == 0xDE);
    REQUIRE(buff[1] == 0xAD);
    REQUIRE(buff[2] == 0xBE);
    REQUIRE(buff[3] == 0xEF);
    // PROGRAM
    REQUIRE(objs.pic->buffer()[10] == 0xDE);
    REQUIRE(objs.pic->buffer()[11] == 0xAD);
    REQUIRE(objs.pic->buffer()[12] == 0xBE);
    REQUIRE(objs.pic->buffer()[13] == 0xEF);
    // USER
    REQUIRE(objs.pic->buffer()[0x00200000] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00200001] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00200002] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00200003] == 0xFF);
    // EEPROM
    REQUIRE(objs.pic->buffer()[0x00380000] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00380001] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00380002] == 0x03);
    REQUIRE(objs.pic->buffer()[0x00380003] == 0x04);
    // CONFIG
    REQUIRE(objs.pic->buffer()[0x00300000] == 0xEC);
    REQUIRE(objs.pic->buffer()[0x00300001] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00300002] == 0x02);
    REQUIRE(objs.pic->buffer()[0x00300003] == 0x03);
  }
  SECTION("erase prog and config and EEPROM") {
    icsp.bulk_erase(Address::Region::PROGRAM | Address::Region::CONFIG |
                    Address::Region::EEPROM);
    icsp.read_n(pic18fq20, 10, buff.begin(), 4);
    REQUIRE(buff[0] == 0xFF);
    REQUIRE(buff[1] == 0xFF);
    REQUIRE(buff[2] == 0xFF);
    REQUIRE(buff[3] == 0xFF);
    // PROGRAM
    REQUIRE(objs.pic->buffer()[10] == 0xFF);
    REQUIRE(objs.pic->buffer()[11] == 0xFF);
    REQUIRE(objs.pic->buffer()[12] == 0xFF);
    REQUIRE(objs.pic->buffer()[13] == 0xFF);
    // USER
    REQUIRE(objs.pic->buffer()[0x00200000] == 0xAB);
    REQUIRE(objs.pic->buffer()[0x00200001] == 0xCD);
    REQUIRE(objs.pic->buffer()[0x00200002] == 0x01);
    REQUIRE(objs.pic->buffer()[0x00200003] == 0x02);
    // EEPROM
    REQUIRE(objs.pic->buffer()[0x00380000] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380001] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380002] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00380003] == 0xFF);
    // CONFIG
    REQUIRE(objs.pic->buffer()[0x00300000] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300001] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300002] == 0xFF);
    REQUIRE(objs.pic->buffer()[0x00300003] == 0xFF);
  }
}

TEST_CASE("less than word size write padded with 0xFF", "[icsp]") {
  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  auto prog = icsp.enter_programming();
  std::vector<uint8_t> data{0xF0, 0x0B, 0x50};
  const auto result =
      icsp.write_verify(pic18fq20, 0x1580, data.begin(), data.end());
  const auto start = pic18q20map::eeprom_region.value.start;
  REQUIRE(objs.pic->buffer()[0x1580] == 0xF0);
  REQUIRE(objs.pic->buffer()[0x1581] == 0x0B);
  REQUIRE(objs.pic->buffer()[0x1582] == 0x50);
  REQUIRE(objs.pic->buffer()[0x1583] == 0xFF);
}