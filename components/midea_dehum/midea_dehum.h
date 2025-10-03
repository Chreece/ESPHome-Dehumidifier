#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  static uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;

  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
