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
  t.set_visual_min_temperature(30);
  t.set_visual_max_temperature(80);
  t.set_visual_temperature_step(1.0f);

  t.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_DRY,
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
  if (call.get_target_temperature().has_value()) {
    int th = std::lroundf(*call.get_target_temperature());
    th = std::clamp(th, 30, 80);
    this->desired_target_humi_ = (uint8_t) th;
    this->target_temperature = th;
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
  size_t total = 10 + payloadLength + 2;
  if (total > sizeof(tx_buf_)) {
    ESP_LOGW(TAG, "TX too large: %u", (unsigned) total);
    return;
  }
  this->build_header_(msgType, agreementVersion, payloadLength);
  memcpy(tx_buf_, header_, 10);
  if (payloadLength) {
    memcpy(tx_buf_ + 10, payload, payloadLength);
  }
  // CRC8 over payload
  tx_buf_[10 + payloadLength] = crc8_payload(tx_buf_ + 10, payloadLength);
  // sum (checksum) over header+payload+CRC8
  tx_buf_[10 + payloadLength + 1] = checksum_sum(tx_buf_, 10 + payloadLength + 1);

  this->write_array(tx_buf_, total);
  this->flush();
  ESP_LOGD(TAG, "TX %u bytes type 0x%02X", (unsigned) total, msgType);
}

void MideaDehumComponent::request_status_() {
  this->send_message_(MSG_GET_STATUS, AGREEMENT_VERSION, 21, GET_STATUS_PAYLOAD);
}

void MideaDehumComponent::send_set_status_() {
  uint8_t pl[25];
  memset(pl, 0, sizeof(pl));
  pl[0] = 0x48;
  pl[1] = this->desired_power_ ? 0x01 : 0x00;
  pl[2] = preset_to_raw(this->desired_preset_) & 0x0F;
  pl[3] = fan_to_raw(this->desired_fan_);
  pl[7] = this->desired_target_humi_;
  this->send_message_(MSG_SET_STATUS, AGREEMENT_VERSION, sizeof(pl), pl);
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
  size_t n = rx_.size();
  if (n < 12) return;
  uint8_t sum_calc = checksum_sum(rx_.data(), n - 1);
  if (sum_calc != rx_[n - 1]) {
    ESP_LOGW(TAG, "SUM mismatch");
    return;
  }
  uint8_t len_byte = rx_[1];
  size_t payload_len = (size_t)(len_byte - 11);
  uint8_t crc_calc = crc8_payload(&rx_[10], payload_len);
  if (crc_calc != rx_[10 + payload_len]) {
    ESP_LOGW(TAG, "CRC mismatch");
    return;
  }

  uint8_t indicator = rx_[10];
  if (indicator == INDICATOR_STATUS) {
    this->decode_status_();
  } else if (indicator == INDICATOR_NET_REQ) {
    // reply net status
    uint8_t net[20];
    memset(net, 0, 20);
    net[0] = 0x01;
    net[1] = 0x01;
    net[2] = 0x04;
    net[3] = 1; net[4] = 0; net[5] = 0; net[6] = 127;
    net[7] = 0xFF;
    net[8] = 0x00; net[9] = 0x00;
    this->send_message_(MSG_NET_RSP, AGREEMENT_VERSION, 20, net);
  } else {
    ESP_LOGD(TAG, "RX indicator 0x%02X", indicator);
  }
}

void MideaDehumComponent::decode_status_() {
  if (rx_.size() <= 32) return;

  bool power = (rx_[11] & 0x01) > 0;
  uint8_t mode_raw = (rx_[12] & 0x0F);
  uint8_t fan_raw = (rx_[13] & 0x7F);
  uint8_t humi_set = (rx_[17] >= 100 ? 99 : rx_[17]);
  uint8_t cur = rx_[26];
  uint8_t err = rx_[31];

  this->mode = power ? climate::CLIMATE_MODE_AUTO : climate::CLIMATE_MODE_OFF;
  this->custom_preset = raw_to_preset(mode_raw);
  this->fan_mode = raw_to_fan(fan_raw);
  this->target_temperature = std::clamp<int>(humi_set, 30, 80);
  this->current_temperature = cur;

  if (error_sensor_) error_sensor_->publish_state(err);

  ESP_LOGD(TAG, "Parsed: pwr=%d mode=%s fan=%u tset=%u cur=%u err=%u",
         (int)power,
         this->custom_preset.has_value() ? this->custom_preset->c_str() : "(none)",
         (unsigned) fan_raw,
         (unsigned) humi_set,
         (unsigned) cur,
         (unsigned) err);

  this->publish_state();
}

// ========== Utility functions ==========

uint8_t MideaDehumComponent::crc8_payload(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

uint8_t MideaDehumComponent::checksum_sum(const uint8_t *data, size_t len) {
  uint32_t s = 0;
  for (size_t i = 0; i < len; i++) s += data[i];
  return static_cast<uint8_t>(s & 0xFF);
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
