#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent;

class IonSwitch : public switch_::Switch {
 public:
  explicit IonSwitch(MideaDehumComponent *parent) : parent_(parent) {}
 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_;
};

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  MideaDehumComponent() : uart::UARTDevice() {}
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }
  void set_tank_full_sensor(binary_sensor::BinarySensor *s) { this->tank_full_sensor_ = s; }
  void set_ion_switch(IonSwitch *s) { this->ion_switch_ = s; }

  void setup() override {}
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override {}

  void set_ion_state(bool state);

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  static uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;

  binary_sensor::BinarySensor *tank_full_sensor_{nullptr};
  IonSwitch *ion_switch_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
