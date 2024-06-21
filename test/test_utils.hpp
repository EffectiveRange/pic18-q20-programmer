#pragma once

#include "MockPIC18Q20.hpp"

#include <ICSP_header.hpp>
#include <IGPIO.hpp>
#include <MockGPIO.hpp>
#include <Region.hpp>

template <typename T> bool in_state(PIC18Q20StateImpl &state) {
  struct prog : SelectiveVisitor {
    void visit(T &p) override { res = true; }
    bool res{false};
  } v;
  state.accept(v);
  return v.res;
}
struct TestObjects {
  TestObjects()
      : gpio(MockGPIO::Create()),
        pic(std::make_shared<MockPIC18Q20>(gpio.get(), ICSPPins{})) {}
  TestObjects(std::nullptr_t) {}
  std::shared_ptr<MockGPIO> gpio;
  std::shared_ptr<MockPIC18Q20> pic;
};
inline auto setup() { return TestObjects{}; }