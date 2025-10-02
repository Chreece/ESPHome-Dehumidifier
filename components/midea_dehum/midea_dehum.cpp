#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

static const uint8_t HDR       = 0xAA;
static const uint8_t CMD_POWER = 0x01;  // DATA: 0x00 off, 0x01 on
static const uint8_t CMD_MODE  = 0x02;  // DATA: 0x00 auto, 0x01 dry(setpoint), 0x02 continuous, 0x03 fan
static const uint8_t CMD_TGT   = 0x04;  // DATA: 30..80

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier (climate) initialized");
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b = this->read();
    rx_buf_.push_back(b);

    // align to header
    while (!rx_buf_.empty() && rx_buf_.front() != HDR)
      rx_buf_.erase(rx_buf_.begin());

    // minimal frame: [HDR][CMD][DATA][CHK]
    if (rx_buf_.size() >= 4) {
      parse_frame_(rx_buf_);
      rx_buf_.clear();
    }
  }
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_humidity(true);
  t.set_supports_humidity(true);
  t.set_min_humidity(30);
  t.set_max_humidity(80);
  t.set_humidity_step(1);
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_AUTO);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);
  // If you later want fan modes via climate: uncomment below and implement handling
  // t.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
  //                            climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});
  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  // Handle HVAC mode
  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF) {
      send_command_({HDR, CMD_POWER, 0x00});
      this->mode = climate::CLIMATE_MODE_OFF;
    } else if (m == climate::CLIMATE_MODE_AUTO) {
      send_command_({HDR, CMD_POWER, 0x01});
      send_command_({HDR, CMD_MODE,  0x00});
      this->mode = climate::CLIMATE_MODE_AUTO;
    } else if (m == climate::CLIMATE_MODE_DRY) {
      send_command_({HDR, CMD_POWER, 0x01});
      send_command_({HDR, CMD_MODE,  0x01});
      this->mode = climate::CLIMATE_MODE_DRY;
    }
  }

  // Handle target humidity
  if (call.get_target_humidity().has_value()) {
    int h = *call.get_target_humidity();
    if (h < 30) h = 30;
    if (h > 80) h = 80;
    send_command_({HDR, CMD_TGT, static_cast<uint8_t>(h)});
    this->target_humidity = h;
  }

  this->publish_state();  // optimistic update
}

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 4) return;
  if (frame[0] != HDR) return;

  uint8_t chk = checksum_(std::vector<uint8_t>(frame.begin(), frame.end() - 1));
  if (chk != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  uint8_t cmd  = frame[1];
  uint8_t data = frame[2];

  switch (cmd) {
    case CMD_POWER:
      this->mode = (data == 0x00) ? climate::CLIMATE_MODE_OFF
                                  : climate::CLIMATE_MODE_AUTO;
      break;

    case CMD_MODE:
      // Map anything non-auto to DRY by default; refine as you add fan/continuous
      this->mode = (data == 0x00) ? climate::CLIMATE_MODE_AUTO
                                  : climate::CLIMATE_MODE_DRY;
      break;

    case CMD_TGT:
      // Treat as both a status echo and current humidity update (adjust to your real protocol)
      this->current_humidity = data;
      this->target_humidity  = data;
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
  for (auto b : out) this->write(b);
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
  uint8_t sum = 0;
  for (auto b : bytes) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
