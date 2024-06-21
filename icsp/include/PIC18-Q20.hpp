// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <Region.hpp>

namespace pic18q20map {
using namespace Address;

constexpr auto program_region_v =
    region{Region::PROGRAM, 0x00'0000, 0x01'0000, 2, 75, true};

constexpr auto user_region_v =
    region{Region::USER, 0x20'0000, 0x20'0040, 2, 75, true};

constexpr auto dia_region_v = region{Region::DIA, 0x2C'0000, 0x2C'0100, 2};

constexpr auto config_region_v =
    region{Region::CONFIG, 0x30'0000, 0x30'0020, 1, 11 * 1000, true, false};

constexpr auto eeprom_region_v =
    region{Region::EEPROM, 0x38'0000, 0x38'0100, 1, 11 * 1000, true};

constexpr auto dci_region_v = region{Region::DCI, 0x3C'0000, 0x3C'000A, 2};

constexpr auto id_region_v = region{Region::ID, 0x3F'FFFC, 0x40'0000, 2};

constexpr auto program_region = region_t<program_region_v>{};
constexpr auto user_region = region_t<user_region_v>{};
constexpr auto dia_region = region_t<dia_region_v>{};
constexpr auto config_region = region_t<config_region_v>{};
constexpr auto eeprom_region = region_t<eeprom_region_v>{};
constexpr auto dci_region = region_t<dci_region_v>{};
constexpr auto id_region = region_t<id_region_v>{};

} // namespace pic18q20map

using pic18fq20_t =
    Address::RegionMap<pic18q20map::program_region_v,
                       pic18q20map::user_region_v, pic18q20map::dia_region_v,
                       pic18q20map::config_region_v,
                       pic18q20map::eeprom_region_v, pic18q20map::dci_region_v,
                       pic18q20map::id_region_v>;

constexpr pic18fq20_t pic18fq20{};