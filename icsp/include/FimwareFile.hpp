// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <Region.hpp>

#include <cstdint>
#include <vector>

using byte_vector = std::vector<uint8_t>;
struct FirmwareFileRegionElem {
  uint32_t base_addr{};
  byte_vector data;
};

struct FirmwareFileRegion {
  Address::region region;
  uint32_t base_addr{};
  std::vector<FirmwareFileRegionElem> elems;
};

using Firmware = std::vector<FirmwareFileRegion>;