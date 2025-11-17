// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos
// <attila.gombos@effective-range.com> SPDX-License-Identifier: MIT

#include "Region.hpp"
#include <IGPIO.hpp>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <ICSP_header.hpp>
#include <utils.hpp>

#include <fmt/format.h>

void ICSPHeader::setup_programming() {
  if (pins.prog_en_pin) {
    igpio->set_gpio_mode(pins.prog_en_pin.value(), IGPIO::Modes::OUTPUT, 0);
  }
}

void ICSPHeader::enable_programming() {
  if (pins.prog_en_pin) {
    igpio->gpio_write(pins.prog_en_pin.value(), 1);
  }
}

void ICSPHeader::disable_programming() {
  if (pins.prog_en_pin) {
    igpio->gpio_write(pins.prog_en_pin.value(), 0);
  }
}

void ICSPHeader::cleanup_gpio() {
  /// Request pins and set up initial values
  igpio->set_gpio_mode(pins.mclr_pin, IGPIO::Modes::OUTPUT, 1);
  igpio->set_gpio_mode(pins.clk_pin, IGPIO::Modes::OUTPUT, 0);
  igpio->set_gpio_mode(pins.data_pin, IGPIO::Modes::OUTPUT, 0);
  setup_programming();
}
ICSPHeader::ICSPHeader(IGPIO::Ptr igp, ICSPPins pins)
    : igpio(std::move(igp)), pins{std::move(pins)} {
  cleanup_gpio();
}

auto ICSPHeader::enter_programming() -> ExitProg {
  using namespace std::chrono_literals;
  if (!m_in_program_mode) {
    cleanup_gpio();
    enable_programming();
    wait(1ms);
    igpio->gpio_write(pins.mclr_pin, 0);
    wait(Timings::T_ENTH * 2);
    constexpr std::uint8_t KEY_SEQ[] = {0x4d_b, 0x43_b, 0x48_b, 0x50_b};
    write_data_sequence(KEY_SEQ);
    wait(Timings::T_ENTH * 2);
    m_in_program_mode = true;
  }
  return ExitProg(*this);
}

void ICSPHeader::load_pc(uint32_t addr) {
  if (addr > 0x3F'FF'FF) {
    throw std::out_of_range("address out of range");
  }
  std::array buff{0_b, 0_b, 0_b};
  write_data_sequence(std::array{0x80_b});
  wait(Timings::T_DLY);
  write_data_sequence(write_cast(addr));
  wait(Timings::T_DLY);
}

void ICSPHeader ::write_data_sequence(std::span<const std::uint8_t> data) {
  constexpr auto CLK_WAIT = std::max(Timings::T_CLK, Timings::T_DS);
  for (auto b : data) {
    const auto byte = std::bitset<8>(b);
    for (auto i = 0; i < 8; ++i) {
      igpio->gpio_write(pins.clk_pin, 1);
      igpio->gpio_write(pins.data_pin, byte[7 - i]);
      wait(CLK_WAIT);
      igpio->gpio_write(pins.clk_pin, 0);
      wait(CLK_WAIT);
    }
  }
}

void ICSPHeader::write_transaction(uint8_t data, bool increment_pc) {
  write_data_sequence(std::array{write_cmd(increment_pc)});
  wait(Timings::T_DLY);
  write_data_sequence(write_cast(data));
}

void ICSPHeader::write_transaction(uint16_t data, bool increment_pc) {
  write_data_sequence(std::array{write_cmd(increment_pc)});
  wait(Timings::T_DLY);
  write_data_sequence(write_cast(data));
}

auto ICSPHeader::read_transaction(bool increment_pc) -> read_t {
  read_t res{};
  const auto cmd = increment_pc ? 0xFE_b : 0xFC_b;
  write_data_sequence(std::array{cmd});
  igpio->set_gpio_mode(pins.data_pin, IGPIO::Modes::INPUT);

  finally restore_data_gpio_mode{[this]() {
    igpio->set_gpio_mode(pins.data_pin, IGPIO::Modes::OUTPUT, 0);
    igpio->gpio_write(pins.clk_pin, 0);
  }};

  wait(std::max(Timings::T_DLY, Timings::T_LZD));

  for (auto byte_cnt = 2; byte_cnt >= 0; --byte_cnt) {
    std::bitset<8> buffer{};
    for (auto bit_idx = 7; bit_idx >= 0; --bit_idx) {
      igpio->gpio_write(pins.clk_pin, 1);
      static_assert(Timings::T_CLK >= Timings::T_CO,
                    "Data out valid time greater than clock half period");
      wait(Timings::T_CLK);
      buffer.set(bit_idx, igpio->gpio_read(pins.data_pin));
      igpio->gpio_write(pins.clk_pin, 0);
      wait(Timings::T_CLK);
    }
    res[byte_cnt] = std::uint8_t(buffer.to_ulong());
  }
  return res;
}

void ICSPHeader::exit_programming() {
  if (m_in_program_mode) {
    wait(Timings::T_ENTH + Timings::T_CLK);
    igpio->gpio_write(pins.mclr_pin, 1);
    disable_programming();
  }
  m_in_program_mode = false;
}

ICSPHeader::~ICSPHeader() {
  exit_programming();
  cleanup_gpio();
}

void ICSPHeader::increment_addr() {
  write_data_sequence(std::array{0xF8_b});
  wait(Timings::T_DLY);
}

void ICSPHeader::bulk_erase(Address::Region region) {
  constexpr auto eeprom_bit = 0;
  constexpr auto prog_bit = 1;
  constexpr auto userid_bit = 2;
  constexpr auto config_bit = 3;
  std::bitset<8> cmd{};
  cmd.set(eeprom_bit,
          (region & Address::Region::EEPROM) != Address::Region::INVALID);
  cmd.set(prog_bit,
          (region & Address::Region::PROGRAM) != Address::Region::INVALID);
  cmd.set(userid_bit,
          (region & Address::Region::USER) != Address::Region::INVALID);
  cmd.set(config_bit,
          (region & Address::Region::CONFIG) != Address::Region::INVALID);
  if (!cmd.any()) {
    return;
  }
  write_data_sequence(std::array{0x18_b});
  wait(Timings::T_DLY);
  write_data_sequence(write_cast(static_cast<uint8_t>(cmd.to_ulong())));
  wait(Timings::T_ERAB);
}