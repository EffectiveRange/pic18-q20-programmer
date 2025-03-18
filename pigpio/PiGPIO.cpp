// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#include "PiGPIO.hpp"

#include <IGPIO.hpp>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>

#include <iostream>
#include <memory>
#include <pigpio.h>
#include <stdexcept>

#include <signal.h>

namespace {
volatile sig_atomic_t s_interrupted = 0;

void catch_signals(int sig) {
  s_interrupted = 1;
  signal(sig, catch_signals);
}

} // namespace

auto IGPIO::Create() -> Ptr { return std::make_shared<PiGPIO>(); }

unsigned int PiGPIO::translate_mode(Modes mode) {
  switch (mode) {
  case IGPIO::Modes::INPUT:
    return PI_INPUT;
  case IGPIO::Modes::OUTPUT:
    return PI_OUTPUT;
  case IGPIO::Modes::ALT0:
    return PI_ALT0;
  case IGPIO::Modes::ALT1:
    return PI_ALT1;
  case IGPIO::Modes::ALT2:
    return PI_ALT2;
  case IGPIO::Modes::ALT3:
    return PI_ALT3;
  case IGPIO::Modes::ALT4:
    return PI_ALT4;
  case IGPIO::Modes::ALT5:
    return PI_ALT5;
  }
  throw std::runtime_error(
      fmt::format("Can't translate mode {}", static_cast<int>(mode)));
}
void PiGPIO::set_gpio_mode(port_id_t port, Modes mode) {
  ensure_running();
  if (const auto res = gpioSetMode(port, translate_mode(mode)); res != 0) {
    const auto msg = fmt::format(
        "Failed to set GPIO mode {} on port {} (error: {}) ", translate_mode(mode), port, res);
    throw std::runtime_error(msg);
  }
}
void PiGPIO::gpio_write(port_id_t gpio, val_t val) {
  ensure_running();
  if (const auto res = gpioWrite(gpio, val); res != 0) {
    const auto msg = fmt::format("Failed to write {} on GPIO {} (error: {})",
                                 val, gpio, res);
    throw std::runtime_error(msg);
  }
}
IGPIO::val_t PiGPIO::gpio_read(port_id_t gpio) {
  ensure_running();
  if (const auto res = gpioRead(gpio); res == PI_BAD_GPIO) {
    const auto msg =
        fmt::format("Failed to read on GPIO {} (error: {})", gpio, res);
    throw std::runtime_error(msg);
  } else {
    return res;
  }
}
void PiGPIO::delay(std::chrono::microseconds d) {
  ensure_running();
  gpioDelay(d.count());
}

PiGPIO::PiGPIO() : m_handle(GPIOLibHandle::instance()) {}

GPIOLibHandle::GPIOLibHandle() {
  PiGPIO::ensure_running();
  // This is needed as the PCM clock interferes with the I2S audio
  // TODO: make this configurable for more versatile use cases
  if (gpioCfgClock(5, PI_CLOCK_PWM, 0) < 0) {
    throw std::runtime_error("Failed to set clock source to PWM");
  }
  if (gpioInitialise() < 0) {
    throw std::runtime_error("Failed to initialize GPIO library");
  }
  initialized = true;
  terminated = false;
}

bool GPIOLibHandle::initialized{false};
bool GPIOLibHandle::terminated{false};

void GPIOLibHandle::terminate() {
  if (initialized && !terminated) {
    gpioTerminate();
    initialized = false;
    terminated = true;
  }
}

GPIOLibHandle::~GPIOLibHandle() { terminate(); }

GPIOLibHandle::Ref GPIOLibHandle::handle{};

void register_signal_handler(int sig, sighandler_t handler) {
  if (::signal(sig, handler) == SIG_ERR) {
    std::cerr << fmt::format("Warning: can't register signal handler for {}!\n",
                             sig);
  }
}

void GPIOLibHandle::atexit_cleanup() {
  if (auto p = weak_instance().lock(); p) {
    p->terminate();
  }
}

auto GPIOLibHandle::weak_instance() -> Ref { return handle; }
auto GPIOLibHandle::instance() -> Ptr {
  PiGPIO::ensure_running();

  if (auto p = handle.lock(); p) {
    return p;
  }
  auto p = Ptr(new GPIOLibHandle{}, [](GPIOLibHandle *p) { delete p; });
  register_signal_handler(SIGINT, catch_signals);
  register_signal_handler(SIGTERM, catch_signals);
  const auto regexit = std::atexit(atexit_cleanup);
  const auto regqexit = std::at_quick_exit(atexit_cleanup);
  if (regexit || regqexit) {
    std::cerr << "Failed to register atexit cleanup function!\n";
  }
  handle = p;
  return p;
}

void PiGPIO::ensure_running() {
  // Don't throw if there's already an exception in-flight
  if (s_interrupted && std::uncaught_exceptions() == 0) {
    throw Interrupted{};
  }
}
