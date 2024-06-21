#include <catch2/catch.hpp>

#include <ICSP_header.hpp>
#include <PIC18-Q20.hpp>
#include <PICProgrammer.hpp>

#include "FimwareFile.hpp"
#include "Region.hpp"
#include "test_utils.hpp"

TEST_CASE("Reading device IDs API", "[PICProgrammer]") {

  auto objs = setup();
  objs.pic->buffer()[0x3FFFFC] = 0x42;
  objs.pic->buffer()[0x3FFFFD] = 0xa0;
  objs.pic->buffer()[0x3FFFFE] = 0x40;
  objs.pic->buffer()[0x3FFFFF] = 0x7a;
  auto icsp = ICSPHeader(objs.gpio);
  PICProgrammer programmer(pic18fq20, icsp);
  const auto [devid, revid] = programmer.read_device_id();
  REQUIRE(devid == 0x7a40);
  REQUIRE(revid == 0xa042);
}

TEST_CASE("Reading DIA API", "[PICProgrammer]") {

  auto objs = setup();
  objs.pic->buffer()[0x2C0000] = 0x42;
  objs.pic->buffer()[0x2C0001] = 0xa0;
  objs.pic->buffer()[0x2C0002] = 0x40;
  objs.pic->buffer()[0x2C0003] = 0x7a;

  objs.pic->buffer()[0x2C0024] = 0x02;
  objs.pic->buffer()[0x2C0025] = 0x01;

  objs.pic->buffer()[0x2C002C] = 0x44;
  objs.pic->buffer()[0x2C002D] = 0x33;

  objs.pic->buffer()[0x2C0032] = 0xBB;
  objs.pic->buffer()[0x2C0033] = 0xAA;

  objs.pic->buffer()[0x2C0036] = 0x22;
  objs.pic->buffer()[0x2C0037] = 0x11;
  objs.pic->buffer()[0x2C0038] = 0x44;
  objs.pic->buffer()[0x2C0039] = 0x33;
  objs.pic->buffer()[0x2C003A] = 0xDD;
  objs.pic->buffer()[0x2C003B] = 0xCC;

  auto icsp = ICSPHeader(objs.gpio);
  PICProgrammer programmer(pic18fq20, icsp);
  const auto dia = programmer.read_dia();
  REQUIRE(dia.mchp_uid[0] == 0xa042);
  REQUIRE(dia.mchp_uid[1] == 0x7a40);
  REQUIRE(dia.low_temp_coeffs.gain == 0x0102);
  REQUIRE(dia.high_temp_coeffs.adc_90 == 0x3344);
  REQUIRE(dia.fixed_voltage_ref[1] == 0xAABB);
  REQUIRE(dia.fixed_voltage_comp[0] == 0x1122);
  REQUIRE(dia.fixed_voltage_comp[1] == 0x3344);
  REQUIRE(dia.fixed_voltage_comp[2] == 0xCCDD);
}

TEST_CASE("Reading DCI API", "[PICProgrammer]") {

  auto objs = setup();
  objs.pic->buffer()[0x3C0000] = 0x80;
  objs.pic->buffer()[0x3C0001] = 0x00;
  objs.pic->buffer()[0x3C0004] = 0x00;
  objs.pic->buffer()[0x3C0005] = 0x01;
  objs.pic->buffer()[0x3C0006] = 0x00;
  objs.pic->buffer()[0x3C0007] = 0x01;
  objs.pic->buffer()[0x3C0008] = 0x14;
  objs.pic->buffer()[0x3C0009] = 0x00;

  auto icsp = ICSPHeader(objs.gpio);
  PICProgrammer programmer(pic18fq20, icsp);
  const auto dci = programmer.read_dci();
  REQUIRE(dci.erase_page_size == 128);
  REQUIRE(dci.num_erasable_pages == 256);
  REQUIRE(dci.eeprom_size == 256);
  REQUIRE(dci.pin_cnt == 20);
}

TEST_CASE("Program Verify API", "[PICProgrammer]") {
  using namespace Address;
  auto objs = setup();
  auto icsp = ICSPHeader(objs.gpio);
  PICProgrammer programmer(pic18fq20, icsp);
  Firmware fw;
  auto &prog = fw.emplace_back(pic18q20map::program_region_v);
  prog.elems.assign({FirmwareFileRegionElem{0, {0xDE, 0xAD, 0xBE, 0xEF}},
                     FirmwareFileRegionElem{0x2120, {0xAA, 0xBB, 0xCC, 0xDD}}});

  auto &conf = fw.emplace_back(pic18q20map::config_region_v);
  conf.elems.assign({FirmwareFileRegionElem{0x00300000,
                                            {
                                                0xEC,
                                                0x01,
                                                0x02,
                                                0x03,
                                                0x9F,
                                                0x40,
                                                0x50,
                                                0x7F,
                                                0x66,
                                                0x77,
                                                0x88,
                                            }},
                     FirmwareFileRegionElem{0x00300018,
                                            {
                                                0xDE,
                                                0xAD,
                                            }}});
  programmer.program_verify(fw);

  REQUIRE(objs.pic->buffer()[0] == 0xDE);
  REQUIRE(objs.pic->buffer()[1] == 0xAD);
  REQUIRE(objs.pic->buffer()[2] == 0xBE);
  REQUIRE(objs.pic->buffer()[3] == 0xEF);

  REQUIRE(objs.pic->buffer()[0x2120] == 0xAA);
  REQUIRE(objs.pic->buffer()[0x2121] == 0xBB);
  REQUIRE(objs.pic->buffer()[0x2122] == 0xCC);
  REQUIRE(objs.pic->buffer()[0x2123] == 0xDD);

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