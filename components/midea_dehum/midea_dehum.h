#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <string>

namespace esphome {
namespace midea_dehum {

// Custom preset string constants
static const char *const PRESET_SMART        = "smart";
static const char *const PRESET_SETPOINT     = "setpoint";
static const char *const PRESET_CONTINUOUS   = "continuous";
static const char *const PRESET_CLOTHES_DRY  = "clothes_dry";

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  MideaDehumComponent() = default;

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }
  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }

 protected:
  // Desired state cache
  bool desired_power_{false};
  uint8_t desired_target_humi_{50};
  climate::ClimateFanMode desired_fan_{climate::CLIMATE_FAN_MEDIUM};
  std::string desired_preset_{PRESET_SMART};

  // UART protocol buffers
  uint8_t header_[10];
  uint8_t tx_buf_[128];
  std::vector<uint8_t> rx_;

  sensor::Sensor *error_sensor_{nullptr};

  // --- protocol helpers ---
  void build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength);
  void send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload);

  void request_status_();
  void send_set_status_();

  void parse_rx_byte_(uint8_t b);
  void try_parse_frame_();
  void decode_status_(const std::vector<uint8_t> &payload);
  void decode_sensors_(const std::vector<uint8_t> &payload);

  uint8_t checksum8_(const std::vector<uint8_t> &bytes, size_t from, size_t to);
  std::vector<uint8_t> build_cmd_(uint8_t msgType, const std::vector<uint8_t> &payload);
  void push_tx_(const std::vector<uint8_t> &frame);

  // Compatibility stubs
  void request_sensors_();
  uint8_t map_preset_to_mode_(const std::string &preset) const;
  std::string map_mode_to_preset_(uint8_t mode) const;
  uint8_t map_fan_to_percent_(climate::ClimateFanMode fan) const;
  climate::ClimateFanMode map_percent_to_fan_(uint8_t pct) const;
};

}  // namespace midea_dehum
}  // namespace esphome
