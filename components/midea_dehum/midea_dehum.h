#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <cstdint>

namespace esphome {
namespace midea_dehum {

// Climate presets we expose as "modes" for the dehumidifier
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
  // ---- UART protocol helpers ----
  void request_status_();                          // send 0x41 status request
  void request_sensors_();                         // send 0xB1 (humidity)
  void send_set_status_();                         // send 0x40 with current target state
  void push_tx_(const std::vector<uint8_t> &frame);
  void parse_rx_byte_(uint8_t b);
  void try_parse_frame_();

  // frame builder/parsers
  static uint8_t checksum8_(const std::vector<uint8_t> &bytes, size_t from, size_t to);
  static std::vector<uint8_t> build_cmd_(uint8_t cmd, const std::vector<uint8_t> &payload);

  // decode current rx frame into state
  void decode_status_(const std::vector<uint8_t> &payload);
  void decode_sensors_(const std::vector<uint8_t> &payload);

  // map climate state -> UART fields
  uint8_t map_preset_to_mode_(const std::string &preset) const;
  std::string map_mode_to_preset_(uint8_t mode) const;
  uint8_t map_fan_to_percent_(climate::ClimateFanMode fan) const;
  climate::ClimateFanMode map_percent_to_fan_(uint8_t fan_pct) const;

  // internal desired state (what we send in 0x40)
  bool desired_power_{true};
  std::string desired_preset_{PRESET_SMART};
  climate::ClimateFanMode desired_fan_{climate::CLIMATE_FAN_MEDIUM};
  uint8_t desired_target_humi_{50};  // 30..80

  // rx assembly buffer
  std::vector<uint8_t> rx_;
  uint32_t last_poll_ms_{0};

  // attached sensors
  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
