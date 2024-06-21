// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#include <fwd.hpp>
// TODO: make these configurable and dependent on the CPU type...
namespace Timings {
using namespace std::chrono_literals;
constexpr auto T_ENTH = 1100us;
constexpr auto T_CLK = 2us;
constexpr auto T_DS = 1us;
constexpr auto T_DLY = 4us;
constexpr auto T_CO = 1us;
constexpr auto T_LZD = 1us;
constexpr auto T_ERAB = 11ms;
} // namespace Timings