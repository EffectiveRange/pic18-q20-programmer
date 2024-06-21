// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <IGPIO.hpp>

#include <optional>

struct ICSPPins {
  std::optional<IGPIO::port_id_t> prog_en_pin = 6;
  IGPIO::port_id_t mclr_pin = 24;
  IGPIO::port_id_t clk_pin = 11;
  IGPIO::port_id_t data_pin = 10;
};