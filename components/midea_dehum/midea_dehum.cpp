#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

<<<<<<< HEAD
// Command bytes (example mapping; adjust to your real protocol if needed)
static const uint8_t CMD_POWER  = 0x01;  // DATA: 0x00=off, 0x01=on
static const uint8_t CMD_MODE   = 0x02;  // DATA: 0x00=auto, 0x01=setpoint(dry), 0x02=continuous, 0x03=fan
static const uint8_t CMD_FAN    = 0x03;  // DATA: 0x00=auto, 0x01=low, 0x02=medium, 0x03=high
static const uint8_t CMD_TARGET = 0x04;  // DATA: 30..80 (%)
static const uint8_t CMD_SWING  = 0x05;  // DATA: bit0 -> 1=on
static const uint8_t CMD_ION    = 0x06;  // DATA: bit0 -> 1=on

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Setting up Midea Dehumidifier (climate)");
  // Optionally: request initial status from device here
=======
// Minimum frame length expected
static const size_t MIN_FRAME_LENGTH = 4;  // header + cmd + data + checksum

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier setup");
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b = this->read();
    rx_buf_.push_back(b);

<<<<<<< HEAD
    // Try to align to header 0xAA
    while (!rx_buf_.empty() && rx_buf_[0] != 0xAA) {
      rx_buf_.erase(rx_buf_.begin());
    }

    // Minimal frame length = 4 bytes
    if (rx_buf_.size() >= 4) {
      // We don't know exact length beyond header/cmd/data/chk for this simplified protocol,
      // so try to parse the entire buffer as a single frame.
      parse_frame_(rx_buf_);
      rx_buf_.clear();
=======
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
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
    }
  }
}

<<<<<<< HEAD
climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  // Dehumidifier supports humidity control (not temperature)
  t.set_supports_current_humidity(true);
  t.set_supports_humidity(true);
  t.set_min_humidity(30);
  t.set_max_humidity(80);
  t.set_humidity_step(1);

  // Map to common HVAC modes: OFF, AUTO, and DRY
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_AUTO);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);

  // You can add FAN modes as "fan modes" via a separate fan component or
  // expose them with selects; here we keep it simple within climate modes.
  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  // Handle mode changes
  if (call.get_mode().has_value()) {
    auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      std::vector<uint8_t> cmd = {0xAA, CMD_POWER, 0x00};
      send_command_(cmd);
    } else if (mode == climate::CLIMATE_MODE_AUTO) {
      // Power ON + AUTO mode
      std::vector<uint8_t> cmd1 = {0xAA, CMD_POWER, 0x01};
      send_command_(cmd1);
      std::vector<uint8_t> cmd2 = {0xAA, CMD_MODE, 0x00};
      send_command_(cmd2);
    } else if (mode == climate::CLIMATE_MODE_DRY) {
      // Power ON + DRY (setpoint) mode
      std::vector<uint8_t> cmd1 = {0xAA, CMD_POWER, 0x01};
      send_command_(cmd1);
      std::vector<uint8_t> cmd2 = {0xAA, CMD_MODE, 0x01};
      send_command_(cmd2);
    }

    // Reflect back in the climate state right away (optimistic)
    this->mode = mode;
    this->publish_state();
  }

  // Handle target humidity
  if (call.get_target_humidity().has_value()) {
    int h = *call.get_target_humidity();
    if (h < 30) h = 30;
    if (h > 80) h = 80;
    std::vector<uint8_t> cmd = {0xAA, CMD_TARGET, static_cast<uint8_t>(h)};
    send_command_(cmd);

    this->target_humidity = h;
    this->publish_state();
  }
}

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  // Expect [0xAA][CMD][DATA][CHK]
  if (frame.size() < 4) {
    ESP_LOGW(TAG, "Frame too short");
    return;
  }
  if (frame[0] != 0xAA) {
    ESP_LOGW(TAG, "Wrong header");
    return;
  }
  uint8_t chk = checksum_(std::vector<uint8_t>(frame.begin(), frame.end() - 1));
  if (chk != frame.back()) {
=======
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
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t cmd = frame[1];
<<<<<<< HEAD
  uint8_t data = frame[2];

  switch (cmd) {
    case CMD_POWER: {
      // 0x00=off, 0x01=on
      this->mode = (data == 0x00) ? climate::CLIMATE_MODE_OFF
                                  : climate::CLIMATE_MODE_AUTO;  // assume auto on power on
      this->publish_state();
      break;
    }
    case CMD_MODE: {
      // 0x00=auto, 0x01=setpoint(dry), 0x02=continuous, 0x03=fan
      if (data == 0x00) this->mode = climate::CLIMATE_MODE_AUTO;
      else this->mode = climate::CLIMATE_MODE_DRY;  // map setpoint/continuous/fan to DRY generally
      this->publish_state();
      break;
    }
    case CMD_TARGET: {
      // Treat as both current and target humidity report for now
      this->current_humidity = data;
      this->target_humidity = data;
      this->publish_state();
      break;
    }
    // Optional: map swing/ion/fan status into separate sensors or attributes if desired
    default:
      ESP_LOGD(TAG, "Unhandled cmd 0x%02X data 0x%02X", cmd, data);
=======
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
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
      break;
  }
}

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
<<<<<<< HEAD
  std::vector<uint8_t> out = cmd;
  out.push_back(checksum_(cmd));
  for (auto b : out) this->write(b);
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
=======
  std::vector<uint8_t> frame = cmd;
  frame.push_back(calculate_checksum_(cmd));
  for (auto b : frame) {
    this->write(b);
  }
}

uint8_t MideaDehumComponent::calculate_checksum_(const std::vector<uint8_t> &cmd) {
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
  uint8_t sum = 0;
  for (auto b : bytes) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
