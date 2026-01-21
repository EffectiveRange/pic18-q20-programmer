// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos
// <attila.gombos@effective-range.com> SPDX-License-Identifier: MIT

#include "libGPIO.hpp"
#include "IGPIO.hpp"

#include <IGPIO.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <thread>

#include <memory>
#include <stdexcept>

#include <signal.h>

#include <gpiod.hpp>

namespace fs = std::filesystem;
namespace {
volatile sig_atomic_t s_interrupted = 0;

void catch_signals(int sig) {
  s_interrupted = 1;
  signal(sig, catch_signals);
}

} // namespace

IGPIO::Ptr IGPIO::Create() { return std::make_shared<LibGPIO>(); }

LibGPIO::LibGPIO(std::string_view device)
    : m_handle(::gpiod::chip(fs::path("/dev") / device)) {}

void LibGPIO::set_gpio_mode(port_id_t port, Modes mode, val_t initial) {
  ensure_running();
  if (mode != Modes::INPUT && mode != Modes::OUTPUT) {
    throw std::runtime_error(
        "Only INPUT and OUTPUT modes are supported for libgpiod for now.");
  }
  auto direction = mode == Modes::INPUT ? gpiod::line::direction::INPUT
                                        : gpiod::line::direction::OUTPUT;
  gpiod::line_settings s;
  if (mode == Modes::OUTPUT) {
    s.set_direction(gpiod::line::direction::OUTPUT);
    s.set_output_value(initial ? gpiod::line::value::ACTIVE
                               : gpiod::line::value::INACTIVE);
  } else {
    s.set_direction(gpiod::line::direction::INPUT);
  }
  auto &req = get_line(port);
  gpiod::line_config cfg;
  cfg.add_line_settings(port, s);
  req.reconfigure_lines(cfg);
}

void LibGPIO::gpio_write(port_id_t gpio, val_t val) {
  ensure_running();
  auto &req = get_line(gpio);
  req.set_value(gpio, val ? gpiod::line::value::ACTIVE
                          : gpiod::line::value::INACTIVE);
}

auto LibGPIO::gpio_read(port_id_t gpio) -> val_t {
  ensure_running();
  auto &req = get_line(gpio);
  return (req.get_value(gpio) == gpiod::line::value::ACTIVE) ? 1 : 0;
}
void LibGPIO::delay(std::chrono::microseconds delay) {

  const auto start = std::chrono::high_resolution_clock::now();
  for (auto now = start; now - start < delay;
       now = std::chrono::high_resolution_clock::now()) {
    std::this_thread::yield();
  }
}

void LibGPIO::ensure_running() {
  // Don't throw if there's already an exception in-flight
  if (s_interrupted && std::uncaught_exceptions() == 0) {
    throw Interrupted{};
  }
}
gpiod::line_request &LibGPIO::get_line(port_id_t gpio) {

  auto it = m_lines.find(gpio);
  if (it != m_lines.end()) {
    return it->second;
  }
  gpiod::line_settings s;
  s.set_direction(gpiod::line::direction::AS_IS);

  auto req = m_handle.prepare_request()
                 .set_consumer("pic18-q20-programmer")
                 .add_line_settings(gpio, s)
                 .do_request();

  auto [ins_it, _] = m_lines.emplace(gpio, std::move(req));
  return ins_it->second;
}
