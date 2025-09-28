#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

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
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b = this->read();
    rx_buf_.push_back(b);

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
    }
  }
}

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
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t cmd = frame[1];
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
      break;
  }
}

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> out = cmd;
  out.push_back(checksum_(cmd));
  for (auto b : out) this->write(b);
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
  uint8_t sum = 0;
  for (auto b : bytes) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
