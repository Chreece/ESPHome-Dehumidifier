#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

// Protocol constants
static constexpr uint8_t HDR_SYNC = 0xAA;
static constexpr uint8_t MSG_SET_STATUS     = 0x02;
static constexpr uint8_t MSG_GET_STATUS     = 0x03;
static constexpr uint8_t MSG_NET_RSP        = 0x0D;
static constexpr uint8_t INDICATOR_STATUS   = 0xC8;
static constexpr uint8_t INDICATOR_NET_REQ  = 0x63;
static constexpr uint8_t AGREEMENT_VERSION  = 0x03;

// Get-status payload as in Hypfer
static const uint8_t GET_STATUS_PAYLOAD[21] = {
  0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03
};

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "setup()");
  // Initialize climate so HA sees it
  this->mode = climate::CLIMATE_MODE_OFF;
  this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  this->target_temperature = desired_target_humi_;
  this->custom_preset = std::string(PRESET_SMART);
  this->publish_state();

  if (this->error_sensor_) this->error_sensor_->publish_state(0);

  // Kick off a status request
  this->request_status_();
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b)) break;
    this->parse_rx_byte_(b);
  }
  // Optionally: periodically poll
  // e.g. every 5 seconds:
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 5000) {
    last = now;
    this->request_status_();
  }
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_temperature(true);

  // Use temperature fields as humidity (%)
  t.set_visual_min_temperature(30.0f);  // minimum humidity %
  t.set_visual_max_temperature(80.0f);  // maximum humidity %

  t.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_DRY,   // treat "dry" as dehumidifying ON
  });
  t.set_supported_fan_modes({
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH
  });
  t.set_supported_custom_presets(std::set<std::string>{
    PRESET_SMART, PRESET_SETPOINT, PRESET_CONTINUOUS, PRESET_CLOTHES_DRY
  });
  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  bool changed = false;

  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    if (this->mode == climate::CLIMATE_MODE_OFF) {
      this->desired_power_ = false;
      changed = true;
    } else if (this->mode == climate::CLIMATE_MODE_DRY) {
      this->desired_power_ = true;
      changed = true;
    }
  }

  // Map humidity % to target_temperature
  if (call.get_target_temperature().has_value()) {
    this->desired_target_humi_ = static_cast<uint8_t>(*call.get_target_temperature());
    this->target_temperature = this->desired_target_humi_;
    changed = true;
  }

  if (call.get_fan_mode().has_value()) {
    this->desired_fan_ = *call.get_fan_mode();
    this->fan_mode = *call.get_fan_mode();
    changed = true;
  }

  if (call.get_custom_preset().has_value()) {
    this->desired_preset_ = *call.get_custom_preset();
    this->custom_preset = *call.get_custom_preset();
    changed = true;
  }

  if (changed) {
    this->send_set_status_();
    this->publish_state();
  }
}

// ========== Protocol: header / send_message ==========

void MideaDehumComponent::build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength) {
  header_[0] = HDR_SYNC;
  header_[1] = static_cast<uint8_t>(10 + payloadLength + 1);
  header_[2] = 0xA1;
  header_[3] = 0x00;
  header_[4] = 0x00;
  header_[5] = 0x00;
  header_[6] = 0x00;
  header_[7] = 0x00;
  header_[8] = agreementVersion;
  header_[9] = msgType;
}

void MideaDehumComponent::send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload) {
  std::vector<uint8_t> frame;

  // --- Header ---
  frame.push_back(0xAA);            // Start
  frame.push_back(0x20);            // Start 2
  frame.push_back(msgType);         // Message type
  frame.push_back(agreementVersion);
  frame.push_back(payloadLength);

  // --- Payload ---
  for (uint8_t i = 0; i < payloadLength; i++) {
    frame.push_back(payload[i]);
  }

  // --- CRC8 of payload ---
  uint8_t crc = crc8_payload(payload, payloadLength);
  frame.push_back(crc);

  // --- Checksum of all previous ---
  uint8_t sum = checksum_sum(frame.data(), frame.size());
  frame.push_back(sum);

  // --- Send over UART ---
  this->write_array(frame);
  this->flush();

  ESP_LOGD(TAG, "Sent frame type=0x%02X len=%u crc=0x%02X sum=0x%02X", msgType, payloadLength, crc, sum);
}

void MideaDehumComponent::request_status_() {
  static const uint8_t GET_STATUS_PAYLOAD[21] = {
      0x01, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  this->send_message_(0x41, 0x01, sizeof(GET_STATUS_PAYLOAD), GET_STATUS_PAYLOAD);

  ESP_LOGD(TAG, "Requested status");
}

void MideaDehumComponent::send_set_status_() {
  uint8_t pl[21] = {0};

  // Byte 0 = Power
  pl[0] = this->desired_power_ ? 0x01 : 0x00;

  // Byte 1 = Mode (mapped from preset)
  pl[1] = preset_to_raw(this->desired_preset_);

  // Byte 2 = Fan speed
  pl[2] = fan_to_raw(this->desired_fan_);

  // Byte 3 = Target humidity (clamped 30–80)
  pl[3] = static_cast<uint8_t>(std::clamp<int>(this->desired_target_humi_, 30, 80));

  this->send_message_(0x40, 0x01, sizeof(pl), pl);

  ESP_LOGD(TAG, "Sent set_status: pwr=%d preset=%s fan=%d humi=%d",
           this->desired_power_,
           this->desired_preset_.c_str(),
           (int)this->desired_fan_,
           (int)this->desired_target_humi_);
}


// ============ Parsing incoming =============

void MideaDehumComponent::parse_rx_byte_(uint8_t b) {
  rx_.push_back(b);
  if (rx_.size() == 1 && rx_[0] != HDR_SYNC) {
    rx_.clear();
    return;
  }
  if (rx_.size() >= 2) {
    uint8_t len_byte = rx_[1];
    if (len_byte < 11) { rx_.clear(); return; }
    size_t payload_len = (size_t)(len_byte - 11);
    size_t total = 10 + payload_len + 2;
    if (total > 256) {
      rx_.clear();
      return;
    }
    if (rx_.size() == total) {
      this->try_parse_frame_();
      rx_.clear();
    } else if (rx_.size() > total) {
      rx_.clear();
    }
  }
}

void MideaDehumComponent::try_parse_frame_() {
  if (rx_.size() < 6) return;  // Not enough for header

  if (rx_[0] != 0xAA || rx_[1] != 0x20) {
    ESP_LOGW(TAG, "Invalid header: %02X %02X", rx_[0], rx_[1]);
    rx_.clear();
    return;
  }

  uint8_t msgType = rx_[2];
  uint8_t agreementVersion = rx_[3];
  uint8_t payloadLength = rx_[4];

  if (rx_.size() < (5 + payloadLength + 2)) {
    // Still incomplete
    return;
  }

  const uint8_t *payload = &rx_[5];
  uint8_t crc = rx_[5 + payloadLength];
  uint8_t sum = rx_[6 + payloadLength];

  // --- Validate CRC ---
  if (crc8_payload(payload, payloadLength) != crc) {
    ESP_LOGW(TAG, "CRC mismatch");
    rx_.clear();
    return;
  }

  // --- Validate SUM ---
  if (checksum_sum(rx_.data(), rx_.size() - 1) != sum) {
    ESP_LOGW(TAG, "Checksum mismatch");
    rx_.clear();
    return;
  }

  // --- Dispatch ---
  if (msgType == 0xC0) {
    decode_status_();
  } else {
    ESP_LOGW(TAG, "Unhandled msgType=0x%02X", msgType);
  }

  rx_.clear();
}

void MideaDehumComponent::decode_status_() {
  if (rx_.size() <= 32) return;

  bool power = (rx_[11] & 0x01) > 0;
  uint8_t mode_raw = (rx_[12] & 0x0F);
  uint8_t fan_raw  = (rx_[13] & 0x7F);
  uint8_t humi_set = (rx_[17] >= 100 ? 99 : rx_[17]);
  uint8_t cur      = rx_[26];
  uint8_t err      = rx_[31];

  // Climate mode: OFF when power=0, DRY when power=1
  this->mode = power ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;

  // Preset and fan
  this->custom_preset = raw_to_preset(mode_raw);
  this->fan_mode = raw_to_fan(fan_raw);

  // Use humidity instead of temperature
  this->target_humidity  = std::clamp<int>(humi_set, 30, 80);
  this->current_humidity = cur;

  // Publish error state
  if (error_sensor_) {
    error_sensor_->publish_state(err);
  }

  ESP_LOGD(TAG, "Parsed: pwr=%d mode=%s fan=%u target_humi=%u cur_humi=%u err=%u",
           (int) power,
           this->custom_preset.has_value() ? this->custom_preset->c_str() : "(none)",
           (unsigned) fan_raw,
           (unsigned) humi_set,
           (unsigned) cur,
           (unsigned) err);

  this->publish_state();
}
// ========== Utility functions ==========

uint8_t MideaDehumComponent::crc8_payload(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x31;
      else
        crc <<= 1;
    }
  }
  return crc;
}

uint8_t MideaDehumComponent::checksum_sum(const uint8_t *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum & 0xFF;
}

climate::ClimateFanMode MideaDehumComponent::raw_to_fan(uint8_t raw) {
  switch (raw) {
    case 1: return climate::CLIMATE_FAN_LOW;
    case 3: return climate::CLIMATE_FAN_HIGH;
    default: return climate::CLIMATE_FAN_MEDIUM;
  }
}

uint8_t MideaDehumComponent::fan_to_raw(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW: return 1;
    case climate::CLIMATE_FAN_HIGH: return 3;
    default: return 2;
  }
}

std::string MideaDehumComponent::raw_to_preset(uint8_t m) {
  switch (m & 0x0F) {
    case 0x01: return PRESET_SETPOINT;
    case 0x02: return PRESET_CONTINUOUS;
    case 0x03: return PRESET_CLOTHES_DRY;
    default: return PRESET_SMART;
  }
}

uint8_t MideaDehumComponent::preset_to_raw(const std::string &p) {
  if (p == PRESET_SETPOINT) return 0x01;
  if (p == PRESET_CONTINUOUS) return 0x02;
  if (p == PRESET_CLOTHES_DRY) return 0x03;
  return 0x00;
}

}  // namespace midea_dehum
}  // namespace esphome
