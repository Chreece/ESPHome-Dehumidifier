#include "midea_dehum.h"
#include "esphome/core/log.h"

static const char *TAG = "midea_dehum";

// ==========================
// Setup
// ==========================
void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier setup started");
}

// ==========================
// Loop: read UART
// ==========================
void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t c = this->read();
    rx_buffer_.push_back(c);

    if (rx_buffer_.size() >= 10) {
      parse_frame_(rx_buffer_);
      rx_buffer_.clear();
    }
  }
}

// ==========================
// Control Methods
// ==========================
void MideaDehumComponent::set_power(bool state) {
  ESP_LOGI(TAG, "Set power: %s", state ? "ON" : "OFF");
  publish_power(state);
  std::vector<uint8_t> cmd = {0xAA, 0x01, state ? 0x01 : 0x00};
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
  std::vector<uint8_t> cmd = {0xAA, 0x05, state ? 0x01 : 0x00};
  send_command_(cmd);
}

void MideaDehumComponent::set_ion(bool state) {
  ESP_LOGI(TAG, "Set ion: %s", state ? "ON" : "OFF");
  publish_ion(state);
  std::vector<uint8_t> cmd = {0xAA, 0x06, state ? 0x01 : 0x00};
  send_command_(cmd);
}

// ==========================
// Publishing Methods
// ==========================
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

// ==========================
// UART Frame Parsing
// ==========================
void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10) return;
  uint8_t checksum = 0;
  for (size_t i = 0; i < frame.size() - 1; ++i) checksum += frame[i];
  if (checksum != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t power = frame[2] & 0x01;
  uint8_t mode = (frame[3] >> 4) & 0x0F;
  uint8_t fan = (frame[3] >> 2) & 0x03;
  uint8_t humidity = frame[4];
  uint8_t swing = (frame[5] >> 5) & 0x01;
  uint8_t ion = (frame[5] >> 6) & 0x01;
  uint8_t tank_full = (frame[6] >> 7) & 0x01;
  uint8_t error = frame[7];

  publish_power(power);
  publish_mode(std::to_string(mode));
  publish_fan(std::to_string(fan));
  publish_current_humidity(humidity);
  publish_swing(swing);
  publish_ion(ion);
  publish_tank_full(tank_full);
  publish_error(std::to_string(error));
}

// ==========================
// UART Command Sending
// ==========================
void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> frame = cmd;
  frame.push_back(calculate_checksum(cmd));
  for (auto b : frame) write(b);
}

uint8_t MideaDehumComponent::calculate_checksum(const std::vector<uint8_t> &cmd) {
  uint8_t sum = 0;
  for (auto b : cmd) sum += b;
  return sum;
}
