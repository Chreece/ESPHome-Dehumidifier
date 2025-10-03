#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>
#include <cstdint>

namespace esphome {
namespace midea_dehum {

// Custom preset names we expose
static const char *const PRESET_SMART        = "smart";
static const char *const PRESET_SETPOINT     = "setpoint";
static const char *const PRESET_CONTINUOUS   = "continuous";
static const char *const PRESET_CLOTHES_DRY  = "clothes_dry";

class MideaDehumComponent : public Component,
                            public climate::Climate,
                            public uart::UARTDevice {
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
  // --- TX helpers ---
  void build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength);
  void send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload);
  void request_status_();
  void send_set_status_();
  void request_sensors_();  // not used but kept for completeness

  // --- RX assembler ---
  void parse_rx_byte_(uint8_t b);
  void try_parse_frame_();

  // --- Decoders ---
  void decode_status_(const std::vector<uint8_t> &payload);
  void decode_sensors_(const std::vector<uint8_t> &payload);

  // --- Compatibility helpers (unused in final but declared in .h) ---
  static uint8_t checksum8_(const std::vector<uint8_t> &bytes, size_t from, size_t to);
  static std::vector<uint8_t> build_cmd_(uint8_t msgType, const std::vector<uint8_t> &payload);
  void push_tx_(const std::vector<uint8_t> &frame);

  uint8_t map_preset_to_mode_(const std::string &preset) const;
  std::string map_mode_to_preset_(uint8_t mode) const;
  uint8_t map_fan_to_percent_(climate::ClimateFanMode fan) const;
  climate::ClimateFanMode map_percent_to_fan_(uint8_t pct) const;

  // --- Buffers/state ---
  std::vector<uint8_t> rx_;
  uint8_t header_[10]{};
  uint8_t tx_buf_[128]{};

  // last poll tick (optional, unused)
  uint32_t last_poll_ms_{0};

  // attached error sensor
  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
