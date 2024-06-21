// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <range/v3/range/concepts.hpp>
#include <range/v3/range/traits.hpp>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/common.hpp>
#include <range/v3/view/subrange.hpp>
#include <utils.hpp>

#include <array>
#include <concepts>
#include <cstdint>
#include <fwd.hpp>
#include <iterator>
#include <span>
#include <stdexcept>

#include <ICSP_pins.hpp>
#include <IGPIO.hpp>
#include <Region.hpp>
#include <Timings.hpp>

struct IProgressListener {
  virtual void onProgress(size_t byteCount) = 0;

protected:
  ~IProgressListener() = default;
};

struct OptListener {
  OptListener(IProgressListener *listener = nullptr) : listener{listener} {}
  void onProgress(size_t byteCount) {
    if (listener)
      listener->onProgress(byteCount);
  }

private:
  IProgressListener *listener{};
};

class ICSPHeader {
public:
  [[nodiscard]] explicit ICSPHeader(IGPIO::Ptr igpio, ICSPPins pins = {});
  ~ICSPHeader();
  using read_t = std::array<std::uint8_t, 3>;
  struct [[nodiscard]] ExitProg {
    explicit ExitProg(ICSPHeader &icsp) : m_icsp{&icsp} {}
    ExitProg(const ExitProg &) = delete;
    ExitProg &operator=(const ExitProg &) = delete;
    ExitProg(ExitProg &&other) noexcept
        : m_icsp{std::exchange(other.m_icsp, nullptr)} {}
    ExitProg &operator=(ExitProg &&other) = delete;

    ~ExitProg() {
      if (m_icsp)
        m_icsp->exit_programming();
    }
    auto &icsp() const { return *m_icsp; }

  private:
    ICSPHeader *m_icsp;
  };
  ExitProg enter_programming();
  void exit_programming();

  // Program/Verify commands
  void load_pc(uint32_t addr);
  void increment_addr();
  void bulk_erase(Address::Region region);

  template <typename Map, typename It>
    requires(std::output_iterator<
                 It, typename std::iterator_traits<It>::value_type> &&
             sizeof(typename std::iterator_traits<It>::value_type) == 1)
  It read_n(Map map, uint32_t addr, It first, std::size_t n,
            OptListener listener = {}) {
    const auto region = region_metadata(map, addr);
    return read_n_impl(region, addr, std::move(first), n, std::move(listener));
  }

  template <typename MemMap, std::input_iterator It, std::sentinel_for<It> S>
    requires(
        std::unsigned_integral<typename std::iterator_traits<It>::value_type> &&
        sizeof(typename std::iterator_traits<It>::value_type) == 1)
  It write(MemMap map, uint32_t addr, It first, S last,
           OptListener listener = {}) {
    const auto region = region_metadata(map, addr);
    load_pc(addr);
    for (auto &&to_write :
         rg::make_subrange(first, last) | rgv::chunk(region.word_size)) {
      write_range(region, to_write);
      if (!region.autoincrement_addr) {
        increment_addr();
      }
      addr += region.word_size;
      listener.onProgress(region.word_size);
    }
    return first;
  }

  template <typename MemMap, std::input_iterator It, std::sentinel_for<It> S>
    requires(
        std::unsigned_integral<typename std::iterator_traits<It>::value_type> &&
        sizeof(typename std::iterator_traits<It>::value_type) == 1)
  It write_verify(MemMap map, uint32_t addr, It first, S last,
                  OptListener listener = {}) {
    const auto region = region_metadata(map, addr);
    load_pc(addr);
    for (auto &&to_write :
         rg::make_subrange(first, last) | rgv::chunk(region.word_size)) {
      write_with_readback(region, addr, to_write);
      increment_addr();
      addr += region.word_size;
      listener.onProgress(region.word_size);
    }
    return first;
  }

  template <Address::region R>
  auto read_region(Address::region_t<R>, OptListener listener = {}) {
    auto res = Address::region_data<R>{};
    read_n_impl(R, R.start, res.data.begin(), res.data.size(),
                std::move(listener));
    return res;
  }

  [[nodiscard]] bool programming() const noexcept { return m_in_program_mode; }

  template <typename T> T read(bool autoinc = true) {
    T val = read_cast<T>(read_transaction(autoinc));
    wait(Timings::T_DLY);
    return val;
  }

  read_t read_raw(bool autoinc = true) {
    read_t res = read_transaction(autoinc);
    wait(Timings::T_DLY);
    return res;
  }

private:
  template <typename It>
  It read_n_impl(Address::region region, uint32_t addr, It first, std::size_t n,
                 OptListener listener = {}) {
    load_pc(addr);
    for (std::size_t i = 0; i < n;
         i += region.word_size, std::advance(first, region.word_size)) {
      auto data = read_cast<2>(read_raw(region.autoincrement_addr));
      std::copy(data.begin(), std::next(data.begin(), region.word_size), first);
      if (!region.autoincrement_addr) {
        increment_addr();
      }
      listener.onProgress(region.word_size);
    }
    return first;
  }
  template <byte_range R>
  void write_with_readback(Address::region region, uint32_t addr,
                           R &&to_write) {
    write_range(region, to_write, false);
    const auto readback = read_cast<2>(read_raw(false));
    auto to_write_common = rg::common_view{to_write};
    if (!std::equal(rg::begin(to_write_common), rg::end(to_write_common),
                    readback.begin())) {
      throw std::runtime_error(fmt::format(
          "Programming error at address 0x{:06x} (Region {}, "
          "word size={})! Wrote 0x{:04x}"
          " but read back is 0x{:04x} ",
          addr, Address::region_to_string(region.name), region.word_size,
          range_cast<uint16_t>(to_write), range_cast<uint16_t>(readback)));
    }
  }

  template <typename T, typename D>
  void write(T data, D wait_dly, bool autoinc = true) {
    write_transaction(data, autoinc);
    wait(wait_dly);
  }

  template <rg::input_range R>
  void write_range(Address::region region, R &&data, bool autoinc) {
    if (const auto rgsize = rg::size(data);
        rgsize == 1 && region.word_size == 1) {
      write_transaction(*rg::begin(data), autoinc);
    } else if (region.word_size == 2 && (rgsize == 2 || rgsize == 1)) {
      uint16_t tmp_lo = *rg::begin(data);
      uint16_t tmp_hi = rgsize == 2 ? *std::next(rg::begin(data)) : 0xFF;
      uint16_t tmp = (tmp_hi << 8) + tmp_lo;
      write_transaction(tmp, autoinc);
    } else {
      throw std::runtime_error("Word size too big for low level write");
    }
    wait(region.prog_delay().value());
  }

  template <rg::input_range R>
  void write_range(Address::region region, R &&data) {
    write_range(region, std::forward<R>(data), region.autoincrement_addr);
  }

  template <typename Map>
  Address::region region_metadata(Map map, uint32_t addr) {
    return Address::with_region(
        addr,
        [addr]<Address::region region>(auto idx, Address::region_t<region>) {
          if (addr % region.word_size != 0) {
            throw std::runtime_error("Unaligned address for region");
          }
          return region;
        },
        map);
  }

  read_t read_transaction(bool increment_pc = true);
  void write_transaction(uint8_t data, bool increment_pc = true);
  void write_transaction(uint16_t data, bool increment_pc = true);

  constexpr auto write_cmd(bool increment_pc) const noexcept {
    const auto cmd = increment_pc ? std::uint8_t{0xE0} : std::uint8_t{0xC0};
    return cmd;
  }

  template <typename Rep, typename Period>
  void wait(std::chrono::duration<Rep, Period> d) {
    using namespace std::chrono;
    igpio->delay(duration_cast<microseconds>(d));
  }

  void setup_programming();
  void enable_programming();
  void disable_programming();

  void cleanup_gpio();
  // Writes the data out on the data lines
  // The data must be in transmission syntax, MSB first, Big Endian format
  // See write_cast() utility for converting native types to transmission format
  void write_data_sequence(std::span<const std::uint8_t> data);

  bool m_in_program_mode = false;
  IGPIO::Ptr igpio;
  ICSPPins pins;
};
