#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Setting up Midea Dehumidifier component");

  // Expose climate immediately
  this->mode = climate::CLIMATE_MODE_OFF;
  this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  this->target_temperature = 50;
  this->current_temperature = 0;
  this->publish_state();

  if (this->error_sensor_) {
    this->error_sensor_->publish_state(0);
  }

  this->send_get_status_();
}

void MideaDehumComponent::loop() {
  this->handle_uart_();
}

climate::ClimateTraits MideaDehumComponent::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);   // we use this for current humidity
  traits.set_supports_target_temperature(true);    // target humidity
  traits.set_visual_min_temperature(30);
  traits.set_visual_max_temperature(80);
  traits.set_visual_temperature_step(1.0f);

  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO});
  traits.set_supported_fan_modes({
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH,
  });

  traits.set_supported_presets({PRESET_SMART, PRESET_SETPOINT, PRESET_CONTINUOUS, PRESET_CLOTHES_DRY});
  return traits;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    this->power_ = this->mode != climate::CLIMATE_MODE_OFF;
  }

  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
    this->target_humidity_ = static_cast<uint8_t>(this->target_temperature);
  }

  if (call.get_fan_mode().has_value()) {
    this->fan_mode = *call.get_fan_mode();
    this->fan_ = this->fan_mode;
  }

  if (call.get_preset().has_value()) {
    this->preset_ = *call.get_preset();
  }

  this->send_set_status_();
  this->publish_state();
}

// ---------------- UART ----------------

void MideaDehumComponent::handle_uart_() {
  while (this->available()) {
    size_t len = this->read_array(this->rx_buf_, sizeof(this->rx_buf_));
    if (len > 30) {
      if (this->rx_buf_[10] == 0xc8) {
        this->parse_state_();
      }
    }
  }
}

void MideaDehumComponent::parse_state_() {
  this->power_ = (this->rx_buf_[11] & 0x01) > 0;
  uint8_t mode = this->rx_buf_[12] & 0x0f;
  this->preset_ = this->map_mode_to_preset_(mode);
  this->fan_ = this->map_proto_to_fan_(this->rx_buf_[13] & 0x7f);

  this->target_humidity_ = this->rx_buf_[17] >= 100 ? 99 : this->rx_buf_[17];
  this->current_humidity_ = this->rx_buf_[26];
  this->error_code_ = this->rx_buf_[31];

  this->mode = this->power_ ? climate::CLIMATE_MODE_AUTO : climate::CLIMATE_MODE_OFF;
  this->target_temperature = this->target_humidity_;
  this->current_temperature = this->current_humidity_;
  this->fan_mode = this->fan_;

  if (this->error_sensor_) {
    this->error_sensor_->publish_state(this->error_code_);
  }

  this->publish_state();
  this->clear_rx_();
}

void MideaDehumComponent::clear_rx_() {
  memset(this->rx_buf_, 0, sizeof(this->rx_buf_));
}

void MideaDehumComponent::build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength) {
  this->header_[0] = 0xAA;
  this->header_[1] = 10 + payloadLength + 1;
  this->header_[2] = 0xA1;
  this->header_[3] = 0x00;
  this->header_[4] = 0x00;
  this->header_[5] = 0x00;
  this->header_[6] = 0x00;
  this->header_[7] = 0x00;
  this->header_[8] = agreementVersion;
  this->header_[9] = msgType;
}

void MideaDehumComponent::send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload) {
  this->build_header_(msgType, agreementVersion, payloadLength);

  memcpy(this->tx_buf_, this->header_, 10);
  memcpy(this->tx_buf_ + 10, payload, payloadLength);
  this->tx_buf_[10 + payloadLength] = 0; // CRC placeholder
  this->tx_buf_[10 + payloadLength + 1] = 0; // checksum placeholder

  this->write_array(this->tx_buf_, 10 + payloadLength + 2);
  this->flush();
}

void MideaDehumComponent::send_get_status_() {
  static const uint8_t getStatusCommand[21] = {
    0x41, 0x81, 0x00, 0xff, 0x03, 0xff,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03
  };
  this->send_message_(0x03, 0x03, 21, getStatusCommand);
}

void MideaDehumComponent::send_set_status_() {
  uint8_t payload[25]{};
  payload[0] = 0x48;
  payload[1] = this->power_ ? 0x01 : 0x00;
  payload[2] = this->map_preset_to_mode_(this->preset_) & 0x0f;
  payload[3] = this->map_fan_to_proto_(this->fan_);
  payload[7] = this->target_humidity_;

  this->send_message_(0x02, 0x03, 25, payload);
}

// -------- mapping ----------

uint8_t MideaDehumComponent::map_preset_to_mode_(const std::string &preset) const {
  if (preset == PRESET_SETPOINT) return 0x01;
  if (preset == PRESET_CONTINUOUS) return 0x02;
  if (preset == PRESET_CLOTHES_DRY) return 0x03;
  return 0x00;
}

std::string MideaDehumComponent::map_mode_to_preset_(uint8_t mode) const {
  switch (mode) {
    case 0x01: return PRESET_SETPOINT;
    case 0x02: return PRESET_CONTINUOUS;
    case 0x03: return PRESET_CLOTHES_DRY;
    default: return PRESET_SMART;
  }
}

uint8_t MideaDehumComponent::map_fan_to_proto_(climate::ClimateFanMode fan) const {
  switch (fan) {
    case climate::CLIMATE_FAN_LOW: return 1;
    case climate::CLIMATE_FAN_HIGH: return 3;
    default: return 2;
  }
}

climate::ClimateFanMode MideaDehumComponent::map_proto_to_fan_(uint8_t raw) const {
  switch (raw) {
    case 1: return climate::CLIMATE_FAN_LOW;
    case 3: return climate::CLIMATE_FAN_HIGH;
    default: return climate::CLIMATE_FAN_MEDIUM;
  }
}

}  // namespace midea_dehum
}  // namespace esphome
