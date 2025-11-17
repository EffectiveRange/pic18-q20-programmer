#include "IGPIO.hpp"
#include "libGPIO.hpp"

#include <chrono>
#include <iostream>

#include <gpiod.hpp>

#include <argparse/argparse.hpp>

#include <fmt/format.h>

int main(int argc, char *argv[]) {

  argparse::ArgumentParser program("libgpio_test");
  program.add_argument("-d", "--device")
      .help("GPIO device to open")
      .default_value("gpiochip0");
  program.parse_args(argc, argv);

  std::cout << "Libgpiod GPIO implementation test\n";
  LibGPIO gpio(program.get<std::string>("--device"));

  gpio.set_gpio_mode(12, IGPIO::Modes::OUTPUT);
  const auto start = std::chrono::high_resolution_clock::now();
  const auto period = std::chrono::microseconds(250); // microseconds
  for (auto now = start; now - start < std::chrono::seconds{2};
       now = std::chrono::high_resolution_clock::now()) {
    gpio.gpio_write(12, 1);
    gpio.delay(period);
    gpio.gpio_write(12, 0);
    gpio.delay(period);
  }
  return 0;
}