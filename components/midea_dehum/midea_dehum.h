#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  MideaDehumComponent() : uart::UARTDevice() {}
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }
  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_cmd3_(uint8_t cmd, uint8_t data);
  static uint8_t checksum_(const std::vector<uint8_t> &bytes);

  // Mapping helpers
  static uint8_t encode_mode_(climate::ClimateMode m, climate::ClimatePreset preset);
  static climate::ClimateMode map_mode_for_ui_(uint8_t proto_mode);  // to display back
  static uint8_t encode_fan_(climate::ClimateFanMode f);
  static climate::ClimateFanMode map_fan_for_ui_(uint8_t proto_fan);

  std::vector<uint8_t> rx_;

  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
