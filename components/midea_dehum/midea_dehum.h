#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <cstdint>
#include <string>

namespace esphome {
namespace midea_dehum {

// Custom presets (strings HA will show)
static const char *const PRESET_SMART        = "smart";
static const char *const PRESET_SETPOINT     = "setpoint";
static const char *const PRESET_CONTINUOUS   = "continuous";
static const char *const PRESET_CLOTHES_DRY  = "clothes_dry";

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  MideaDehumComponent() = default;

  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }
  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

  // Component
  void setup() override;
  void loop() override;

  // Climate
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  // UART helpers
  void request_status_();
  void send_set_status_();
  void parse_rx_(const std::vector<uint8_t> &frame);

  // Mapping
  uint8_t preset_to_raw_(const std::string &p) const;
  std::string raw_to_preset_(uint8_t raw) const;
  uint8_t fan_to_raw_(climate::ClimateFanMode f) const;
  climate::ClimateFanMode raw_to_fan_(uint8_t raw) const;

  // Desired state
  bool desired_power_{true};
  std::string desired_preset_{PRESET_SMART};
  climate::ClimateFanMode desired_fan_{climate::CLIMATE_FAN_MEDIUM};
  uint8_t desired_target_humi_{50};

  // RX buffer
  std::vector<uint8_t> rx_;

  // Attached sensor
  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
