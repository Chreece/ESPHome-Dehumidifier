#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

// Minimum frame length expected
static const size_t MIN_FRAME_LENGTH = 4;  // header + cmd + data + checksum

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier setup");
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t c = this->read();
    rx_buffer_.push_back(c);

    // Keep enough bytes to parse
    if (rx_buffer_.size() >= MIN_FRAME_LENGTH) {
      // Try to locate the header byte (0xAA)
      size_t start = 0;
      while (start < rx_buffer_.size() && rx_buffer_[start] != 0xAA) {
        start++;
      }
      if (start > 0) {
        rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + start);
      }
      
      // Now check if we have at least header + cmd + data + checksum
      if (rx_buffer_.size() >= MIN_FRAME_LENGTH) {
        parse_frame_(rx_buffer_);
        rx_buffer_.clear();  // clear for now after parsing
      }
    }
  }
}

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

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < MIN_FRAME_LENGTH) {
    ESP_LOGW(TAG, "Frame too short for parse");
    return;
  }
  if (frame[0] != 0xAA) {
    ESP_LOGW(TAG, "Header byte mismatch");
    return;
  }

  // Validate checksum
  uint8_t checksum = 0;
  for (size_t i = 0; i < frame.size() - 1; ++i) {
    checksum += frame[i];
  }
  if (checksum != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t cmd = frame[1];
  uint8_t data = 0;
  if (frame.size() >= 3) {
    data = frame[2];
  }

  switch (cmd) {
    case 0x01:
      publish_power((data & 0x01) != 0);
      break;
    case 0x02: {
      std::string m = "auto";
      if (data == 0x01) m = "setpoint";
      else if (data == 0x02) m = "continuous";
      else if (data == 0x03) m = "fan";
      publish_mode(m);
      break;
    }
    case 0x03: {
      std::string f = "auto";
      if (data == 0x01) f = "low";
      else if (data == 0x02) f = "medium";
      else if (data == 0x03) f = "high";
      publish_fan(f);
      break;
    }
    case 0x04:
      publish_current_humidity(static_cast<float>(data));
      break;
    case 0x05:
      publish_swing((data & 0x01) != 0);
      break;
    case 0x06:
      publish_ion((data & 0x01) != 0);
      break;
    default:
      ESP_LOGD(TAG, "Unknown cmd: 0x%02X", cmd);
      break;
  }
}

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> frame = cmd;
  frame.push_back(calculate_checksum_(cmd));
  for (auto b : frame) {
    this->write(b);
  }
}

uint8_t MideaDehumComponent::calculate_checksum_(const std::vector<uint8_t> &cmd) {
  uint8_t sum = 0;
  for (auto b : cmd) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
