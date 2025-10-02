#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

static const uint8_t HEADER = 0xAA;
static const uint8_t CMD_POWER  = 0x01;
static const uint8_t CMD_MODE   = 0x02;
static const uint8_t CMD_TARGET = 0x04;

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier initialized");
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b = this->read();
    rx_buf_.push_back(b);

    // Sync to header
    while (!rx_buf_.empty() && rx_buf_[0] != HEADER) {
      rx_buf_.erase(rx_buf_.begin());
    }

    // Expect [HEADER][CMD][DATA][CHK]
    if (rx_buf_.size() >= 4) {
      parse_frame_(rx_buf_);
      rx_buf_.clear();
    }
  }
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_humidity(true);
  t.set_supports_target_humidity(true);
  t.set_visual_min_humidity(30);
  t.set_visual_max_humidity(80);
  t.set_visual_humidity_step(1);
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_AUTO);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);
  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      send_command_({HEADER, CMD_POWER, 0x00});
      this->mode = climate::CLIMATE_MODE_OFF;
    } else if (mode == climate::CLIMATE_MODE_AUTO) {
      send_command_({HEADER, CMD_POWER, 0x01});
      send_command_({HEADER, CMD_MODE, 0x00});
      this->mode = climate::CLIMATE_MODE_AUTO;
    } else if (mode == climate::CLIMATE_MODE_DRY) {
      send_command_({HEADER, CMD_POWER, 0x01});
      send_command_({HEADER, CMD_MODE, 0x01});
      this->mode = climate::CLIMATE_MODE_DRY;
    }
  }

  if (call.get_target_humidity().has_value()) {
    int h = *call.get_target_humidity();
    if (h < 30) h = 30;
    if (h > 80) h = 80;
    send_command_({HEADER, CMD_TARGET, static_cast<uint8_t>(h)});
    this->target_humidity = h;
  }

  this->publish_state();
}

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 4) return;
  if (frame[0] != HEADER) return;

  uint8_t chk = checksum_(std::vector<uint8_t>(frame.begin(), frame.end() - 1));
  if (chk != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t cmd = frame[1];
  uint8_t data = frame[2];

  switch (cmd) {
    case CMD_POWER:
      this->mode = (data == 0) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_AUTO;
      break;
    case CMD_MODE:
      this->mode = (data == 0) ? climate::CLIMATE_MODE_AUTO : climate::CLIMATE_MODE_DRY;
      break;
    case CMD_TARGET:
      this->current_humidity = data;
      this->target_humidity = data;
      break;
    default:
      ESP_LOGD(TAG, "Unhandled cmd 0x%02X data 0x%02X", cmd, data);
      break;
  }
  this->publish_state();
}

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> out = cmd;
  out.push_back(checksum_(cmd));
  for (auto b : out) {
    this->write(b);
  }
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
  uint8_t sum = 0;
  for (auto b : bytes) {
    sum += b;
  }
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
