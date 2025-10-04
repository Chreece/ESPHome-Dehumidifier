#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_IONIZER
#include "esphome/components/switch/switch.h"
#endif

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
  virtual ~MideaDehumComponent();

  climate::ClimateTraits traits() override;  

  void set_uart(uart::UARTComponent *uart);

  void control(const climate::ClimateCall &call) override;
  void setup() override;
  void loop() override;

  void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

#ifdef USE_IONIZER
  void set_ionizer_switch(switch_::Switch *s) { ionizer_switch_ = s; }
  void set_ionizer_state(bool state) {
    desired_ionizer_ = state;
    send_set_status_();
  }
#endif

 protected:
  uart::UARTComponent *uart_{nullptr};
  // Protocol fields
  uint8_t header_[10];
  uint8_t tx_buf_[128];
  std::vector<uint8_t> rx_;

  // Desired state fields
  bool desired_power_{false};
  float desired_target_humi_{50.0f};
  climate::ClimateFanMode desired_fan_{climate::CLIMATE_FAN_MEDIUM};
  std::string desired_preset_{PRESET_SMART};

#ifdef USE_IONIZER
  bool desired_ionizer_{false};
  switch_::Switch *ionizer_switch_{nullptr};
#endif

  sensor::Sensor *error_sensor_{nullptr};

  // Protocol helpers
  void build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength);
  void send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload);

  void request_status_();
  void send_set_status_();

  void parse_rx_byte_(uint8_t b);
  void try_parse_frame_();
  void decode_status_(const std::vector<uint8_t> &frame);
  void read_uart_data();

  // Utility

  size_t calculate_frame_length(const std::vector<uint8_t> &buf);
  static uint8_t crc8_payload(const uint8_t *data, size_t len);
  static uint8_t checksum_sum(const uint8_t *data, size_t len);

  static climate::ClimateFanMode raw_to_fan(uint8_t raw);
  static uint8_t fan_to_raw(climate::ClimateFanMode f);
  static std::string raw_to_preset(uint8_t raw);
  static uint8_t preset_to_raw(const std::string &p);
};

#ifdef USE_IONIZER
class IonizerSwitch : public switch_::Switch, public Parented<MideaDehumComponent> {
 protected:
  void write_state(bool state) override {
    this->parent_->set_ionizer_state(state);
    this->publish_state(state);
  }
};
#endif

}  // namespace midea_dehum
}  // namespace esphome
