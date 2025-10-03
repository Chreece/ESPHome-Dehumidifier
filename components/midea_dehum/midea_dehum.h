#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <string>

namespace esphome {
namespace midea_dehum {

static const char *const PRESET_SMART       = "smart";
static const char *const PRESET_SETPOINT    = "setpoint";
static const char *const PRESET_CONTINUOUS  = "continuous";
static const char *const PRESET_CLOTHES_DRY = "clothes_dry";

class MideaDehumComponent : public climate::Climate, public Component, public uart::UARTDevice {
  public:
  MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  // Must add set_uart so the Python side can wire it
  void set_uart(uart::UARTComponent *parent) { this->set_uart_parent(parent); }
  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

  void setup() override;
  void loop() override;

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  // --- Protocol fields ---
  uint8_t header_[10];
  uint8_t tx_buf_[128];
  std::vector<uint8_t> rx_;

  // Desired state fields
  bool desired_power_{false};
  uint8_t desired_target_humi_{50};
  climate::ClimateFanMode desired_fan_{climate::CLIMATE_FAN_MEDIUM};
  std::string desired_preset_{PRESET_SMART};

  // Error sensor
  sensor::Sensor *error_sensor_{nullptr};

  // Protocol helpers
  void build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength);
  void send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload);

  void request_status_();
  void send_set_status_();

  void parse_rx_byte_(uint8_t b);
  void try_parse_frame_();
  void decode_status_();
  // decode_sensors_ is optional, may not be needed

  // Utility
  static uint8_t crc8_payload(const uint8_t *data, size_t len);
  static uint8_t checksum_sum(const uint8_t *data, size_t len);

  static climate::ClimateFanMode raw_to_fan(uint8_t raw);
  static uint8_t fan_to_raw(climate::ClimateFanMode f);
  static std::string raw_to_preset(uint8_t raw);
  static uint8_t preset_to_raw(const std::string &p);
};

}  // namespace midea_dehum
}  // namespace esphome
