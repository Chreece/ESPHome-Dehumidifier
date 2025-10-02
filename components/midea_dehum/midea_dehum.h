#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  // Default ctor required by ESPHome codegen (it calls new MideaDehumComponent();)
  MideaDehumComponent() : uart::UARTDevice() {}
  // Optional convenience ctor
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  // Called from to_code() to attach UART after default construction
  void set_uart(uart::UARTComponent *parent) { this->set_parent(parent); }

  // Component lifecycle
  void setup() override;
  void loop() override;

  // Climate API
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  // UART helpers
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  static uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;
};

}  // namespace midea_dehum
}  // namespace esphome
