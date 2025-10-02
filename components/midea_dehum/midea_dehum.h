#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  MideaDehumComponent() : uart::UARTDevice() {}
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }

  // Extra setters for Python binding
  void set_tank_full_sensor(binary_sensor::BinarySensor *s) { this->tank_full_sensor_ = s; }
  void set_ion_switch(switch_::Switch *s) { this->ion_switch_ = s; }
  void set_swing_switch(switch_::Switch *s) { this->swing_switch_ = s; }

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  static uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;

  // Entities
  binary_sensor::BinarySensor *tank_full_sensor_{nullptr};
  switch_::Switch *ion_switch_{nullptr};
  switch_::Switch *swing_switch_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
