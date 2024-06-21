// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#include "MockPIC18Q20.hpp"
#include "IGPIO.hpp"
#include "IntelHex.hpp"
#include "Region.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>

void IDLE::prog_en_rising() {
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<PROG_EN>(m_state);
}

void PROG_EN::mclr_falling() {
  if (m_state->now.count() > 0 &&
      (m_state->last_clk_change.count() > 0 &&
           m_state->now - m_state->last_clk_change < T_ENTS ||
       m_state->last_data_change.count() > 0 &&
           m_state->now - m_state->last_data_change < T_ENTS)) {
    throw std::runtime_error("Timing violation");
  }
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<MCLR>(m_state);
}
void PROG_EN::prog_en_falling() {
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<IDLE>(m_state);
}

void MCLR::mclr_rising() {
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<PROG_EN>(m_state);
}
void MCLR::on_data(uint32_t data) {

  if (data != 0x4d434850) {
    throw std::runtime_error("Invalid programming sequence");
  }
  to_programming(T_ENTH + T_CKL);
}

void PROGRAMMING::mclr_rising() {
  if (m_state->now - m_state->last_clk_falling < m_ts + T_ENTH + T_CKL) {
    throw std::runtime_error("Programming exit hold time error");
  }

  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<PROG_EN>(m_state);
}
void PROGRAMMING::clk_rising() {
  if (m_state->now - m_state->last_clk_falling < m_ts) {
    throw std::runtime_error("Timing violation on Prog entry CLK HIGH");
  }
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<COMMAND_PREAMBLE>(m_state);
}

void COMMAND_PREAMBLE::on_data(uint32_t data) {
  const auto cmd = static_cast<uint8_t>(data);
  auto curr = std::move(m_state->prog_state);
  switch (cmd) {
  case 0b1000'0000:
    m_state->prog_state = std::make_unique<LOAD_PC>(m_state);
    break;
  case 0b1111'1100:
    m_state->prog_state = std::make_unique<READ_NVM>(m_state, false);
    break;
  case 0b1111'1110:
    m_state->prog_state = std::make_unique<READ_NVM>(m_state, true);
    break;
  case 0b1100'0000:
    m_state->prog_state = std::make_unique<WRITE>(m_state, false);
    break;
  case 0b1110'0000:
    m_state->prog_state = std::make_unique<WRITE>(m_state, true);
    break;
  case 0b0001'1000:
    m_state->prog_state = std::make_unique<BULK_ERASE>(m_state);
    break;
  case 0b1111'1000:
    m_state->prog_state = std::make_unique<INC_PC>(m_state);
    break;
  default:
    throw std::runtime_error("Unknown ICSP command");
  }
}

void LOAD_PC::on_data(uint32_t data) {
  data >>= 1;
  data &= 0x3FFFFF;
  this->m_state->pc = data;
  to_programming();
}

PIC18Q20StateImpl *
PIC18Q20StateImpl::to_programming(std::chrono::nanoseconds setup_timeout) {
  m_state->icspdat.client_mode = IGPIO::Modes::INPUT;
  auto curr = std::move(m_state->prog_state);
  m_state->prog_state = std::make_unique<PROGRAMMING>(m_state, setup_timeout);
  return m_state->prog_state.get();
}

READ_NVM::READ_NVM(PIC18Q20State *m_state, bool increment_pc)
    : Visitable<READ_NVM>(m_state), ReadWriteBase(m_state),
      m_inc_pc{increment_pc && auto_inc_addr}, clk_cnt{init_clk} {
  m_state->icspdat.client_mode = IGPIO::Modes::OUTPUT;
  Address::with_region(
      addr,
      [&]<Address::region region>(auto idx, Address::region_t<region>) {
        word_size = region.word_size;
        region_end = region.end;
      },
      pic18fq20);

  for (uint32_t i = 0; i < word_size; ++i) {
    if (addr + i >= region_end) {
      throw std::runtime_error("Accessing cross region data");
    }
    data |= static_cast<uint8_t>(m_state->buffer[addr + i]) << (i * 8);
  }
}

void READ_NVM::clk_rising() {
  if (clk_cnt == init_clk) {
    if (m_state->now - m_state->last_clk_falling < T_DLY) {
      throw std::runtime_error("Command delay violation");
    }
  }
  if (--clk_cnt > word_bits()) {
    m_state->icspdat.set_value(0);
  } else if (clk_cnt > 0) {
    m_state->icspdat.set_value(data.test(clk_cnt - 1) ? 1 : 0);
  } else {
    // stop bit
    m_state->icspdat.set_value(0);
  }
}

void READ_NVM::clk_falling() {
  if (clk_cnt == 0) {
    if (m_inc_pc) {
      this->m_state->pc.value() += word_size;
    }
    to_programming();
  }
}

WRITE::WRITE(PIC18Q20State *m_state, bool increment_pc)
    : ReceiveData<WRITE, 24>(m_state), ReadWriteBase(m_state),
      m_inc_pc{increment_pc && auto_inc_addr} {
  if (addr + word_size > region_end) {
    throw std::runtime_error("Writing cross region data");
  }
}

void WRITE::on_data(uint32_t data) {
  // shift out stop bit;
  data >>= 1;

  if (word_size == 1) {
    this->m_state->buffer[addr] = static_cast<uint8_t>(data & 0xFF);
  } else if (word_size == 2) {
    this->m_state->buffer[addr] = static_cast<uint8_t>(data & 0xFF);
    this->m_state->buffer[addr + 1] =
        static_cast<uint8_t>((data & 0xFF00) >> 8);
  } else {
    throw std::runtime_error("Unhandled word size in write mock");
  }
  if (m_inc_pc) {
    this->m_state->pc.value() += word_size;
  }
  to_programming(t_prog);
}
val_t ICSPDatPin::onRead(MockGPIO::GPIOState &st) {
  if (host_mode != IGPIO::Modes::INPUT && client_mode != IGPIO::Modes::OUTPUT) {
    throw std::runtime_error(
        fmt::format("Collision on ICSPDAT line  during host read "));
  }
  if (state->now - state->last_clk_rising < T_CO ||
      state->now - state->last_data_change < T_CO) {
    throw std::runtime_error("T_CO violation on data read");
  }
  return m_value.value();
}
void ICSPDatPin::onWrite(MockGPIO::GPIOState &st, val_t v) {
  if (host_mode != IGPIO::Modes::OUTPUT && client_mode != IGPIO::Modes::INPUT) {
    throw std::runtime_error("Collision on ICSPDAT line during write");
  }
  auto &ld = this->state->last_data_latch;
  if (ld.has_value() && this->state->now - ld.value() < T_DH) {
    throw std::runtime_error("Timing violation T_DH");
  }
  ld = std::nullopt;
  m_value = v;
  state->last_data_change = state->now;
}
void ICSPDatPin::onModeChange(MockGPIO::GPIOState &state, IGPIO::Modes mode) {
  if (state.mode != mode) {
    m_value = std::nullopt;
    host_mode = mode;
    this->state->last_data_change = this->state->now;
  }
}
void ICSPDatPin::onWait(std::chrono::microseconds d) { state->now += d; }
std::optional<val_t> ICSPDatPin::value() const {
  if (host_mode != IGPIO::Modes::OUTPUT && client_mode != IGPIO::Modes::INPUT) {
    throw std::runtime_error(
        fmt::format("Collision on ICSPDAT line during client read "));
  }
  return m_value;
}
void ICSPDatPin::set_value(std::optional<val_t> val) {

  if (host_mode != IGPIO::Modes::INPUT && client_mode != IGPIO::Modes::OUTPUT) {
    throw std::runtime_error(
        fmt::format("Collision on ICSPDAT line during client write "));
  }
  m_value = val;
  state->last_data_change = state->now;
}

void BULK_ERASE::on_data(uint32_t data) {
  // shift out stop bit;
  data >>= 1;
  if ((data & 0b0001) != 0) {
    m_state->buffer.fill_region(Address::Region::EEPROM, 0xFF);
  }

  if ((data & 0b0010) != 0) {
    m_state->buffer.fill_region(Address::Region::PROGRAM, 0xFF);
  }

  if ((data & 0b0100) != 0) {
    m_state->buffer.fill_region(Address::Region::USER, 0xFF);
  }

  if ((data & 0b1000) != 0) {
    m_state->buffer.fill_region(Address::Region::CONFIG, 0xFF);
  }

  to_programming(T_ERAB);
}

INC_PC::INC_PC(PIC18Q20State *m_state)
    : Visitable<INC_PC>(m_state), ReadWriteBase(m_state) {}

void INC_PC::clk_rising() {
  this->m_state->pc.value() += word_size;
  auto *prog = to_programming();
  prog->clk_rising();
}

void INC_PC::mclr_rising() {
  auto *prog = to_programming();
  prog->mclr_rising();
}

MockPIC18Q20::MockPIC18Q20(MockGPIO *gpio, ICSPPins pins)
    : gpio(std::move(gpio)), pins{pins}, clk{this}, prog(this), mclr(this),
      m_state(std::make_unique<PIC18Q20State>(this)) {
  m_state->prog_state = std::make_unique<IDLE>(m_state.get());
  this->gpio->set_pin_listener(pins.clk_pin, &clk);
  this->gpio->set_pin_listener(pins.prog_en_pin.value(), &prog);
  this->gpio->set_pin_listener(pins.mclr_pin, &mclr);
  this->gpio->set_pin_listener(pins.data_pin, &m_state->icspdat);
}
MockPIC18Q20::~MockPIC18Q20() {
  this->gpio->set_pin_listener(pins.clk_pin);
  this->gpio->set_pin_listener(pins.prog_en_pin.value());
  this->gpio->set_pin_listener(pins.mclr_pin);
  this->gpio->set_pin_listener(pins.data_pin);
  if (const auto outfile = std::getenv("MOCK_GPIO_OUTPUT_HEX"); outfile) {
    std::ofstream ofs(outfile);
    IntelHex::Dumper dumper(ofs);
    m_state->buffer.dump(dumper);
  }
}
