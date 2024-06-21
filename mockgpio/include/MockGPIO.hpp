// SPDX-FileCopyrightText: 2024 Ferenc Nandor Janky <ferenj@effective-range.com>
// SPDX-FileCopyrightText: 2024 Attila Gombos <attila.gombos@effective-range.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <IGPIO.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>

class MockPIC18Q20;

class GPIOLibHandle {
private:
  struct CtorTag {};

public:
  using Ptr = std::shared_ptr<GPIOLibHandle>;
  using Ref = std::weak_ptr<GPIOLibHandle>;
  static Ptr instance();
  static Ref weak_instance();

  GPIOLibHandle(CtorTag const &) : GPIOLibHandle() {}

  static bool set_fail_to_init(bool val) {
    return std::exchange(fail_to_initialize, val);
  }

  bool is_initialized() const noexcept { return initialized; }

  bool is_terminated() const noexcept { return terminated; }

private:
  void terminate();
  static void atexit_cleanup();

  static Ref handle;
  static bool fail_to_initialize;
  static bool initialized;
  static bool terminated;

  GPIOLibHandle();
  ~GPIOLibHandle();
};

struct MockGPIO : public IGPIO {
  struct GPIOState;
  struct PinListener {
    virtual void onWrite(GPIOState &state, val_t v) = 0;
    virtual val_t onRead(GPIOState &state) = 0;
    virtual void onModeChange(GPIOState &state, Modes mode) = 0;
    virtual void onWait(std::chrono::microseconds d) = 0;

  protected:
    ~PinListener() = default;
  };

  MockGPIO();
  struct GPIOState {
    const port_id_t id;
    Modes mode;
    std::optional<val_t> val;
    PinListener *listener{};
  };

  static void ensure_running();

  void set_gpio_mode(port_id_t port, Modes mode) override;

  void gpio_write(port_id_t gpio, val_t val) override;

  val_t gpio_read(port_id_t gpio) override;
  void delay(std::chrono::microseconds) override;

  void set_pin_listener(port_id_t p, PinListener *listener = nullptr);

  static std::shared_ptr<MockGPIO> Create();

  std::optional<GPIOState> get_state(port_id_t);

  std::unique_ptr<MockPIC18Q20> pic{};

  void set_outfile(std::string_view f) { m_out_filename = f; }

  void load_mock_buffer();

private:
  using GPIOStates = std::map<port_id_t, GPIOState>;
  GPIOStates m_gpios;
  GPIOLibHandle::Ptr m_handle;
  std::optional<std::string_view> m_out_filename;
};
