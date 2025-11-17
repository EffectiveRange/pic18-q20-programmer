// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos
// <attila.gombos@effective-range.com> SPDX-License-Identifier: MIT

#pragma once

#include <IGPIO.hpp>

#include <gpiod.hpp>
#include <map>
#include <memory>

struct LibGPIO : public IGPIO {

  LibGPIO(std::string_view device = "gpiochip0");

  static void ensure_running();

  void set_gpio_mode(port_id_t port, Modes mode, val_t initial) override;
  using IGPIO::set_gpio_mode;

  void gpio_write(port_id_t gpio, val_t val) override;

  val_t gpio_read(port_id_t gpio) override;
  void delay(std::chrono::microseconds) override;

private:
  gpiod::line &get_line(port_id_t gpio);
  gpiod::chip m_handle;
  std::map<port_id_t, gpiod::line> m_lines;
};
