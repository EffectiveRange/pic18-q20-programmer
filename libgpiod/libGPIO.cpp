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
#include <fmt/format.h>
#include <thread>

#include <memory>
#include <stdexcept>

#include <signal.h>

#include <gpiod.hpp>

namespace {
volatile sig_atomic_t s_interrupted = 0;

void catch_signals(int sig) {
  s_interrupted = 1;
  signal(sig, catch_signals);
}

} // namespace

IGPIO::Ptr IGPIO::Create() { return std::make_shared<LibGPIO>(); }

LibGPIO::LibGPIO(std::string_view device)
    : m_handle(::gpiod::chip(std::string(device))) {}

void LibGPIO::set_gpio_mode(port_id_t port, Modes mode, val_t initial) {
  ensure_running();
  if (mode != Modes::INPUT && mode != Modes::OUTPUT) {
    throw std::runtime_error(
        "Only INPUT and OUTPUT modes are supported for libgpiod for now.");
  }
  auto direction = mode == Modes::INPUT ? gpiod::line::DIRECTION_INPUT
                                        : gpiod::line::DIRECTION_OUTPUT;
  auto &line = get_line(port);
  if (direction == gpiod::line::DIRECTION_OUTPUT) {
    line.set_direction_output(initial);
  } else {
    line.set_direction_input();
  }
}

void LibGPIO::gpio_write(port_id_t gpio, val_t val) {
  ensure_running();
  get_line(gpio).set_value(val);
}

auto LibGPIO::gpio_read(port_id_t gpio) -> val_t {
  ensure_running();
  return get_line(gpio).get_value();
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
gpiod::line &LibGPIO::get_line(port_id_t gpio) {

  auto it = m_lines.find(gpio);
  if (it != m_lines.end()) {
    return it->second;
  }
  auto line = m_handle.get_line(gpio);
  line.request({.consumer = "pic18-q20-programmer",
                .request_type = gpiod::line_request::DIRECTION_AS_IS});
  auto res = m_lines.insert({gpio, std::move(line)});
  return res.first->second;
}
