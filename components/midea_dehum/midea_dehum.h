#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <cstdint>

namespace esphome {
namespace midea_dehum {

static const char *const PRESET_SMART        = "smart";
static const char *const PRESET_SETPOINT     = "setpoint";
static const char *const PRESET_CONTINUOUS   = "continuous";
static const char *const PRESET_CLOTHES_DRY  = "clothes_dry";

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }
  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

  void setup() override;
  void loop() override;

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void handle_uart_();
  void parse_state_();
  void clear_rx_();
  void send_get_status_();
  void send_set_status_();

  // Helpers
  void build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength);
  void send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload);

  // Map climate values <-> protocol
  uint8_t map_preset_to_mode_(const std::string &preset) const;
  std::string map_mode_to_preset_(uint8_t mode) const;
  uint8_t map_fan_to_proto_(climate::ClimateFanMode fan) const;
  climate::ClimateFanMode map_proto_to_fan_(uint8_t raw) const;

  // Buffers
  uint8_t rx_buf_[256]{};
  uint8_t tx_buf_[256]{};
  uint8_t header_[10]{};

  // State
  bool power_{false};
  std::string preset_{PRESET_SMART};
  climate::ClimateFanMode fan_{climate::CLIMATE_FAN_MEDIUM};
  uint8_t target_humidity_{50};
  uint8_t current_humidity_{0};
  uint8_t error_code_{0};

  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
