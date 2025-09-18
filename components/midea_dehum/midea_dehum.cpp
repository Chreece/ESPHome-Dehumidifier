#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

// ========== Setup ==========
void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier component setup");
  // nothing else here — you can attach YAML template entities to this component's id
}

// ========== Loop / UART read ==========
void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t c = this->read();
    rx_buffer_.push_back(c);

    // Simple framing: attempt parse when buffer length reaches a minimum.
    // NOTE: This is conservative — adjust parsing to match exact protocol of your model.
    if (rx_buffer_.size() >= 6) {
      // try to find header 0xAA at start (if not at start, drop until header)
      size_t start = 0;
      while (start < rx_buffer_.size() && rx_buffer_[start] != 0xAA) start++;
      if (start > 0) {
        // drop leading bytes
        rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + start);
      }

      // If we still have at least 4 bytes, treat as potential frame: [0xAA][cmd][data...][chk]
      // For robustness you should implement exact frame length detection. Here we guard against tiny buffers.
      if (rx_buffer_.size() >= 4) {
        // try parse the entire buffer as a single frame for now
        parse_frame_(rx_buffer_);
        rx_buffer_.clear();
      }
    }
  }
}

// ========== Control methods (called from YAML template lambdas) ==========
void MideaDehumComponent::set_power(bool state) {
  ESP_LOGI(TAG, "Set power: %s", state ? "ON" : "OFF");
  publish_power(state);
  std::vector<uint8_t> cmd = {0xAA, 0x01, static_cast<uint8_t>(state ? 0x01 : 0x00)};
  send_command_(cmd);
}

void MideaDehumComponent::set_mode(const std::string &mode) {
  ESP_LOGI(TAG, "Set mode: %s", mode.c_str());
  publish_mode(mode);
  uint8_t mode_byte = 0x00;
  if (mode == "setpoint") mode_byte = 0x01;
  else if (mode == "continuous") mode_byte = 0x02;
  else if (mode == "fan") mode_byte = 0x03;
  std::vector<uint8_t> cmd = {0xAA, 0x02, mode_byte};
  send_command_(cmd);
}

void MideaDehumComponent::set_fan(const std::string &fan) {
  ESP_LOGI(TAG, "Set fan: %s", fan.c_str());
  publish_fan(fan);
  uint8_t fan_byte = 0x00;
  if (fan == "low") fan_byte = 0x01;
  else if (fan == "medium") fan_byte = 0x02;
  else if (fan == "high") fan_byte = 0x03;
  std::vector<uint8_t> cmd = {0xAA, 0x03, fan_byte};
  send_command_(cmd);
}

void MideaDehumComponent::set_target_humidity(int humidity) {
  if (humidity < 30) humidity = 30;
  if (humidity > 80) humidity = 80;
  ESP_LOGI(TAG, "Set target humidity: %d", humidity);
  publish_current_humidity(humidity);
  std::vector<uint8_t> cmd = {0xAA, 0x04, static_cast<uint8_t>(humidity)};
  send_command_(cmd);
}

void MideaDehumComponent::set_swing(bool state) {
  ESP_LOGI(TAG, "Set swing: %s", state ? "ON" : "OFF");
  publish_swing(state);
  std::vector<uint8_t> cmd = {0xAA, 0x05, static_cast<uint8_t>(state ? 0x01 : 0x00)};
  send_command_(cmd);
}

void MideaDehumComponent::set_ion(bool state) {
  ESP_LOGI(TAG, "Set ion: %s", state ? "ON" : "OFF");
  publish_ion(state);
  std::vector<uint8_t> cmd = {0xAA, 0x06, static_cast<uint8_t>(state ? 0x01 : 0x00)};
  send_command_(cmd);
}

// ========== Publishing helpers ==========
void MideaDehumComponent::publish_current_humidity(float value) {
  if (current_humidity_) current_humidity_->publish_state(value);
}
void MideaDehumComponent::publish_error(const std::string &err) {
  if (error_sensor_) error_sensor_->publish_state(err);
}
void MideaDehumComponent::publish_tank_full(bool state) {
  if (tank_full_) tank_full_->publish_state(state);
}
void MideaDehumComponent::publish_power(bool state) {
  if (power_switch_) power_switch_->publish_state(state);
}
void MideaDehumComponent::publish_mode(const std::string &mode) {
  if (mode_select_) mode_select_->publish_state(mode);
}
void MideaDehumComponent::publish_fan(const std::string &fan) {
  if (fan_select_) fan_select_->publish_state(fan);
}
void MideaDehumComponent::publish_swing(bool state) {
  if (swing_switch_) swing_switch_->publish_state(state);
}
void MideaDehumComponent::publish_ion(bool state) {
  if (ion_switch_) ion_switch_->publish_state(state);
}

// ========== UART parsing ==========
void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  // Very small generic parser — replace with exact protocol from Hypfer's repo for full compatibility
  if (frame.size() < 4) {
    ESP_LOGW(TAG, "Frame too small to parse");
    return;
  }

  // validate header
  if (frame[0] != 0xAA) {
    ESP_LOGW(TAG, "Frame header mismatch");
    return;
  }

  // verify checksum: last byte is checksum
  if (frame.size() < 3) return;
  uint8_t checksum = 0;
  for (size_t i = 0; i < frame.size() - 1; ++i) checksum += frame[i];
  if (checksum != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  // Interpret: [0xAA][cmd][data...][chk]
  uint8_t cmd = frame[1];

  switch (cmd) {
    case 0x01: {  // power or power-state frame
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      bool power = (val & 0x01);
      publish_power(power);
      break;
    }
    case 0x02: {  // mode
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      // map numeric mode to strings (example mapping)
      std::string m = "auto";
      if (val == 0x01) m = "setpoint";
      else if (val == 0x02) m = "continuous";
      else if (val == 0x03) m = "fan";
      publish_mode(m);
      break;
    }
    case 0x03: {  // fan
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      std::string f = "auto";
      if (val == 0x01) f = "low";
      else if (val == 0x02) f = "medium";
      else if (val == 0x03) f = "high";
      publish_fan(f);
      break;
    }
    case 0x04: {  // humidity reading or setpoint
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      publish_current_humidity(static_cast<float>(val));
      break;
    }
    case 0x05: {  // swing
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      publish_swing((val & 0x01) != 0);
      break;
    }
    case 0x06: {  // ion
      uint8_t val = frame.size() > 2 ? frame[2] : 0;
      publish_ion((val & 0x01) != 0);
      break;
    }
    default:
      ESP_LOGD(TAG, "Unknown command byte: 0x%02X", cmd);
      break;
  }
}

// ========== Send command ==========
void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> frame = cmd;
  frame.push_back(calculate_checksum(cmd));
  for (auto b : frame) {
    this->write(b);
  }
}

uint8_t MideaDehumComponent::calculate_checksum(const std::vector<uint8_t> &cmd) {
  uint8_t sum = 0;
  for (auto b : cmd) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
