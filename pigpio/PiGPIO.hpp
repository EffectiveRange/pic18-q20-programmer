// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <IGPIO.hpp>

#include <memory>
#include <pigpio.h>

class GPIOLibHandle {
private:
  struct CtorTag {};

public:
  using Ptr = std::shared_ptr<GPIOLibHandle>;
  using Ref = std::weak_ptr<GPIOLibHandle>;
  static Ptr instance();
  static Ref weak_instance();

  GPIOLibHandle(CtorTag const &) : GPIOLibHandle() {}

private:
  void terminate();
  static void atexit_cleanup();

  static Ref handle;
  static bool initialized;
  static bool terminated;

  GPIOLibHandle();
  ~GPIOLibHandle();
};

struct PiGPIO : public IGPIO {

  PiGPIO();

  static unsigned int translate_mode(Modes mode);
  static void ensure_running();

  void set_gpio_mode(port_id_t port, Modes mode) override;

  void gpio_write(port_id_t gpio, val_t val) override;

  val_t gpio_read(port_id_t gpio) override;
  void delay(std::chrono::microseconds) override;

private:
  GPIOLibHandle::Ptr m_handle;
};
