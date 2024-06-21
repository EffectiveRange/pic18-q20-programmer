// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <Region.hpp>
#include <span>

struct IDumper {
  virtual void dump_start() = 0;
  virtual void dump_end() = 0;
  virtual void dump_region(Address::Region reg, std::span<uint8_t> data) = 0;

protected:
  ~IDumper() = default;
};