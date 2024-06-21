// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ICSP_pins.hpp"
#include "IDumper.hpp"
#include "IGPIO.hpp"
#include "MockGPIO.hpp"
#include "PIC18-Q20.hpp"
#include "Region.hpp"

#include <bitset>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

using port_id_t = IGPIO::port_id_t;
using val_t = IGPIO::val_t;

using namespace std::chrono_literals;

// Electrical specs
static constexpr auto T_ENTS = 100ns;
static constexpr auto T_ENTH = 1ms;
static constexpr auto T_CKL = 100ns;
static constexpr auto T_CKH = 100ns;
static constexpr auto T_DS = 100ns;
static constexpr auto T_DH = 100ns;
static constexpr auto T_CO = 80ns;
static constexpr auto T_LZD = 80ns;
static constexpr auto T_HZD = 80ns;
static constexpr auto T_DLY = 1us;
static constexpr auto T_ERAB = 11ms;
static constexpr auto T_ERAS = 11ms;
static constexpr auto T_PDFM = 11ms;
static constexpr auto T_PINT = 75us;
static constexpr auto T_EXIT = 1us;

struct IPIC18Q20;
struct PIC18Q20StateImpl;

template <typename T> struct TD;

template <typename T, size_t N> auto filled_array(T const &init = {}) {
  std::array<T, N> arr;
  std::fill(arr.begin(), arr.end(), init);
  return arr;
}

template <typename T> struct region_buffer;

template <Address::is_region_c Reg, Reg R>
struct region_buffer<constant<Reg, R>> {
  std::array<uint8_t, R.size()> data = filled_array<uint8_t, R.size()>(0xff);
};

template <auto... R>
  requires(... && Address::is_region_c<decltype(R)>)
struct mem_buffer {

  auto &operator[](uint32_t addr) {
    return Address::with_region(
        addr,
        [&]<size_t Idx, Address::region region>(
            std::integral_constant<size_t, Idx>,
            Address::region_t<region>) -> auto & {
          auto &r = std::get<Idx>(m_data);
          return r.data.at(region.rel_addr(addr));
        },
        regions());
  }

  auto region(Address::Region name) {
    return Address::with_region(
        name,
        [&]<size_t Idx, Address::region region>(
            std::integral_constant<size_t, Idx>,
            Address::region_t<region>) -> std::span<uint8_t> {
          auto &r = std::get<Idx>(m_data);
          return r.data;
        },
        regions());
  }

  void fill_region(Address::Region name, uint8_t val) {
    auto r = region(name);
    std::fill(r.begin(), r.end(), 0xFF);
  }

  void dump(IDumper &dumper) {
    dumper.dump_start();
    (dumper.dump_region(R.name, region(R.name)), ...);
    dumper.dump_end();
  }

private:
  constexpr auto regions() { return Address::RegionMap<R...>{}; }
  std::tuple<region_buffer<Address::region_t<R>>...> m_data{};
};

template <typename T> struct make_mem_buffer;
template <auto... R> struct make_mem_buffer<Address::RegionMap<R...>> {
  using type = mem_buffer<R...>;
};
using PinListener = MockGPIO::PinListener;

class MockPIC18Q20;
struct PIC18Q20State;
struct ICSPDatPin : PinListener {
  explicit ICSPDatPin(MockPIC18Q20 *s, PIC18Q20State *state)
      : pic{s}, state{state} {}
  val_t onRead(MockGPIO::GPIOState &st) override;
  void onWrite(MockGPIO::GPIOState &st, val_t v) override;
  void onModeChange(MockGPIO::GPIOState &state, IGPIO::Modes mode) override;
  void onWait(std::chrono::microseconds d) override;
  std::optional<val_t> value() const;
  void set_value(std::optional<val_t> val);
  IGPIO::Modes client_mode{IGPIO::Modes::INPUT};

private:
  MockPIC18Q20 *pic{};
  PIC18Q20State *state{};
  IGPIO::Modes host_mode{IGPIO::Modes::UNDEFINED};

  std::optional<val_t> m_value;
};

// TODO: dump to file based on environment variable (hex, bin)
struct PIC18Q20State {
  PIC18Q20State(MockPIC18Q20 *s) : pic{s}, icspdat(s, this) {}
  ////////////////
  make_mem_buffer<std::remove_const_t<decltype(pic18fq20)>>::type buffer;
  std::optional<uint32_t> pc{};
  std::unique_ptr<PIC18Q20StateImpl> prog_state;
  MockPIC18Q20 *pic{};
  ICSPDatPin icspdat;
  using us = std::chrono::microseconds;
  us now{};
  us last_mclr_change{};
  us last_clk_change{};
  us last_data_change{};
  us last_clk_rising{};
  us last_clk_falling{};
  us last_mclr_rising{};
  us last_mclr_falling{};
  std::optional<us> last_data_latch{};
};

struct IPIC18Q20 {

  virtual void clk_rising() = 0;
  virtual void clk_falling() = 0;
  virtual void mclr_rising() = 0;
  virtual void mclr_falling() = 0;
  virtual void prog_en_rising() = 0;
  virtual void prog_en_falling() = 0;
  virtual ~IPIC18Q20() = default;
};

template <typename T> inline constexpr auto as_nanos(T duration) noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
}

template <std::size_t HI_TIME_NS = 0, std::size_t LO_TIME_NS = 0>
struct ControlPinListenerBase : PinListener {
  using PinListener::PinListener;

  void onModeChange(MockGPIO::GPIOState &state,
                    IGPIO::Modes mode) override final {}
  void onWait(std::chrono::microseconds d) override final { m_now += d; }

  val_t onRead(MockGPIO::GPIOState &) override final {
    throw std::logic_error("output only pin");
  }
  void onWrite(MockGPIO::GPIOState &st, val_t v) override final {
    if (st.val == 1 && v == 0) {
      if (m_now.count() > 0 &&
          as_nanos(m_now - m_lastchange).count() < HI_TIME_NS) {
        throw std::logic_error("Timing error");
      }
      m_lastchange = m_now;
      m_lastrising = m_now;
      onFalling(st, v);
    } else if (st.val == 0 && v == 1) {
      if (m_now.count() > 0 &&
          as_nanos(m_now - m_lastchange).count() < LO_TIME_NS) {
        throw std::logic_error("Timing error");
      }
      m_lastchange = m_now;
      m_lastfalling = m_now;
      onRising(st, v);
    }
  }

  auto last_rising() const noexcept { return m_lastrising; }
  auto last_falling() const noexcept { return m_lastfalling; }

private:
  virtual void onRising(MockGPIO::GPIOState &st, val_t v) = 0;
  virtual void onFalling(MockGPIO::GPIOState &st, val_t v) = 0;

  std::chrono::microseconds m_lastchange{};
  std::chrono::microseconds m_lastrising{};
  std::chrono::microseconds m_lastfalling{};
  std::chrono::microseconds m_now{};
};

template <typename T, auto RisingFun, auto FallingFun,
          std::size_t HI_TIME_NS = 0, std::size_t LO_TIME_NS = 0>
struct ControlPinListener : ControlPinListenerBase<HI_TIME_NS, LO_TIME_NS> {
  ControlPinListener(T *self) : self{self} {}

private:
  void onRising(MockGPIO::GPIOState &st, val_t v) override {
    (self->*RisingFun)();
  }

  void onFalling(MockGPIO::GPIOState &st, val_t v) override {
    (self->*FallingFun)();
  }
  T *self{};
};

struct StateVisitor;

struct PIC18Q20StateImpl : IPIC18Q20 {
  explicit PIC18Q20StateImpl(PIC18Q20State *m_state) : m_state(m_state) {}

  virtual void accept(StateVisitor &) = 0;

  void clk_rising() override { invalid_pulse(); }
  void clk_falling() override { invalid_pulse(); }
  void mclr_rising() override { invalid_pulse(); }
  void mclr_falling() override { invalid_pulse(); }
  void prog_en_rising() override { invalid_pulse(); }
  void prog_en_falling() override { invalid_pulse(); }
  ~PIC18Q20StateImpl() override{};
  [[noreturn]] void invalid_pulse() const {
    throw std::runtime_error("Invalid state transition");
  }
  PIC18Q20StateImpl *
  to_programming(std::chrono::nanoseconds setup_timeout = T_DLY);

protected:
  PIC18Q20StateImpl(const PIC18Q20StateImpl &) = default;
  PIC18Q20StateImpl &operator=(const PIC18Q20StateImpl &) = default;

  PIC18Q20State *m_state;
};

struct IDLE;
struct PROG_EN;
struct MCLR;
struct PROGRAMMING;
struct COMMAND_PREAMBLE;
struct LOAD_PC;
struct BULK_ERASE;
struct PAGE_ERASE;
struct READ_NVM;
struct INC_PC;
struct WRITE;
struct PROG_ACCESS_EN;

struct StateVisitor {

  virtual void visit(IDLE &) = 0;
  virtual void visit(PROG_EN &) = 0;
  virtual void visit(MCLR &) = 0;
  virtual void visit(PROGRAMMING &) = 0;
  virtual void visit(COMMAND_PREAMBLE &) = 0;
  virtual void visit(LOAD_PC &) = 0;
  virtual void visit(BULK_ERASE &) = 0;
  virtual void visit(PAGE_ERASE &) = 0;
  virtual void visit(READ_NVM &) = 0;
  virtual void visit(INC_PC &) = 0;
  virtual void visit(WRITE &) = 0;
  virtual void visit(PROG_ACCESS_EN &) = 0;

protected:
  ~StateVisitor() = default;
};

struct SelectiveVisitor : StateVisitor {
  void visit(IDLE &) override {}
  void visit(PROG_EN &) override {}
  void visit(MCLR &) override {}
  void visit(PROGRAMMING &) override {}
  void visit(COMMAND_PREAMBLE &) override {}
  void visit(LOAD_PC &) override {}
  void visit(BULK_ERASE &) override {}
  void visit(PAGE_ERASE &) override {}
  void visit(READ_NVM &) override {}
  void visit(INC_PC &) override {}
  void visit(WRITE &) override {}
  void visit(PROG_ACCESS_EN &) override {}
};

template <typename Derived> struct Visitable : PIC18Q20StateImpl {
  using PIC18Q20StateImpl::PIC18Q20StateImpl;
  void accept(StateVisitor &visitor) override {
    visitor.visit(*static_cast<Derived *>(this));
  }
};

struct IDLE : Visitable<IDLE> {
  using Visitable<IDLE>::Visitable;
  void prog_en_rising() override;
  ~IDLE() = default;
};

struct PROG_EN : Visitable<PROG_EN> {
  using Visitable<PROG_EN>::Visitable;

  void mclr_falling() override;
  void prog_en_falling() override;
};

template <typename Derived, size_t DataN,
          std::size_t ENTRY_T_NS = as_nanos(T_DLY).count()>
struct ReceiveData : Visitable<Derived> {
  using Visitable<Derived>::Visitable;
  void clk_rising() final {
    if (m_initial &&
        as_nanos(this->m_state->now - this->m_state->last_clk_falling).count() <
            ENTRY_T_NS) {
      throw std::runtime_error("CLK SETUP TIME ERRROR!");
    }
    m_initial = false;
  }

  void clk_falling() final {
    if (this->m_state->now - this->m_state->last_data_change < T_DS) {
      throw std::runtime_error("Timing violation T_DS");
    }
    auto data = this->m_state->icspdat.value().value();
    if (cnt >= DataN) {
      throw std::runtime_error("Extra data received");
    }
    this->m_state->last_data_latch = this->m_state->now;
    this->data <<= 1;
    this->data |= (data & 1);
    ++cnt;
    if (cnt == DataN) {
      static_cast<Derived *>(this)->on_data(this->data);
    }
  }

private:
  uint32_t data{};
  uint32_t cnt{};
  bool m_initial{true};
};

struct MCLR : ReceiveData<MCLR, 32, as_nanos(T_ENTH).count()> {
  using ReceiveData<MCLR, 32, as_nanos(T_ENTH).count()>::ReceiveData;

  void mclr_rising() override;
  void on_data(uint32_t data);
};

struct PROGRAMMING : Visitable<PROGRAMMING> {
  PROGRAMMING(PIC18Q20State *m_state, std::chrono::nanoseconds ts)
      : Visitable<PROGRAMMING>(m_state), m_ts(ts) {}

  void mclr_rising() override;
  void clk_rising() override;

private:
  std::chrono::nanoseconds m_ts{};
};

struct COMMAND_PREAMBLE : ReceiveData<COMMAND_PREAMBLE, 8, 0> {
  using ReceiveData<COMMAND_PREAMBLE, 8, 0>::ReceiveData;
  void on_data(uint32_t data);
};

struct LOAD_PC : ReceiveData<LOAD_PC, 24> {
  using ReceiveData<LOAD_PC, 24>::ReceiveData;
  void on_data(uint32_t data);
};

struct ReadWriteBase {
  explicit ReadWriteBase(PIC18Q20State *m_state) : addr{m_state->pc.value()} {
    Address::with_region(
        addr,
        [&]<Address::region region>(auto idx, Address::region_t<region>) {
          word_size = region.word_size;
          region_end = region.end;
          t_prog = std::chrono::microseconds{region.t_PROG_us};
          auto_inc_addr = region.autoincrement_addr;
        },
        pic18fq20);
  }

protected:
  uint32_t addr{};
  uint32_t word_size{};
  uint32_t region_end{};
  std::chrono::microseconds t_prog{};
  bool auto_inc_addr{};
};

struct READ_NVM : Visitable<READ_NVM>, ReadWriteBase {
  READ_NVM(PIC18Q20State *m_state, bool increment_pc);
  static constexpr uint32_t init_clk = 24;

  uint32_t word_bits() const noexcept { return word_size * 8; };

  void clk_rising() override;
  void clk_falling() override;

private:
  bool m_inc_pc{};
  uint32_t clk_cnt{};
  std::bitset<32> data{};
};

struct WRITE : ReceiveData<WRITE, 24>, ReadWriteBase {
  WRITE(PIC18Q20State *m_state, bool increment_pc);
  void on_data(uint32_t data);

private:
  bool m_inc_pc{};
};

struct BULK_ERASE : ReceiveData<BULK_ERASE, 24> {
  using ReceiveData<BULK_ERASE, 24>::ReceiveData;
  void on_data(uint32_t data);
};

struct INC_PC : Visitable<INC_PC>, ReadWriteBase {
  INC_PC(PIC18Q20State *m_state);
  void clk_rising() override;
  void mclr_rising() override;
};

class MockPIC18Q20 {
public:
  MockPIC18Q20(MockGPIO *gpio, ICSPPins pins);

  ~MockPIC18Q20();

  MockPIC18Q20(const MockPIC18Q20 &) = delete;
  MockPIC18Q20 &operator=(const MockPIC18Q20 &) = delete;

  void clk_rising() {
    m_state->last_clk_rising = m_state->last_clk_change = m_state->now;
    return m_state->prog_state->clk_rising();
  }
  void clk_falling() {
    m_state->last_clk_falling = m_state->last_clk_change = m_state->now;
    return m_state->prog_state->clk_falling();
  }
  void mclr_rising() {
    m_state->last_mclr_rising = m_state->last_mclr_change = m_state->now;
    m_state->prog_state->mclr_rising();
  }
  void mclr_falling() {
    m_state->last_mclr_falling = m_state->last_mclr_change = m_state->now;
    m_state->prog_state->mclr_falling();
  }
  void prog_en_rising() { m_state->prog_state->prog_en_rising(); }
  void prog_en_falling() { m_state->prog_state->prog_en_falling(); }

  PIC18Q20StateImpl &state() { return *m_state->prog_state; }
  auto &buffer() { return m_state->buffer; }
  auto pc() { return m_state->pc; }
  auto get_gpio() const { return gpio; }

private:
  using ClkListener =
      ControlPinListener<MockPIC18Q20, &MockPIC18Q20::clk_rising,
                         &MockPIC18Q20::clk_falling, 100, 100>;
  using ProgListener =
      ControlPinListener<MockPIC18Q20, &MockPIC18Q20::prog_en_rising,
                         &MockPIC18Q20::prog_en_falling>;
  using MclrListener =
      ControlPinListener<MockPIC18Q20, &MockPIC18Q20::mclr_rising,
                         &MockPIC18Q20::mclr_falling>;
  MockGPIO *gpio{};
  ICSPPins pins;
  ClkListener clk;
  ProgListener prog;
  MclrListener mclr;
  std::unique_ptr<PIC18Q20State> m_state{};
};
