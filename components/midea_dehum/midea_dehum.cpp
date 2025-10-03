#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

MideaDehumComponent::MideaDehumComponent(uart::UARTComponent *uart) : uart_(uart) {}

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
  size_t len = uart_->available();
  if (len > 0) {
    std::vector<uint8_t> buf(len);
    uart_->read_array(buf.data(), len);
    ESP_LOGD(TAG, "RX bulk %u bytes", (unsigned)len);
    for (size_t i = 0; i < len; i++) {
      ESP_LOGD(TAG, " 0x%02X", buf[i]);
      rx_.push_back(buf[i]);
    }
  }
  this->try_parse_frame_();

  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 5000) {
    last = now;
    ESP_LOGD(TAG, "UART available = %u", this->available());
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

void MideaDehumComponent::request_status_() {
  // Hypfer-compatible "get status" frame
  static const uint8_t payload[11] = {
    0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF,
    0x00, 0x02, 0x00, 0x00, 0x00
  };

  // Build frame: header (10) + payload (11) + crc8 (1) + checksum (1)
  const uint8_t payload_len = sizeof(payload);
  const uint8_t frame_len   = 10 + payload_len + 2;

  std::vector<uint8_t> frame(frame_len);

  // Header
  frame[0] = 0xAA;
  frame[1] = 10 + payload_len + 1;   // length byte: header+payload+crc
  frame[2] = 0xA1;                   // appliance type
  frame[3] = 0x00;
  frame[4] = 0x00;
  frame[5] = 0x00;
  frame[6] = 0x00;
  frame[7] = 0x00;
  frame[8] = 0x03;                   // agreement version (Hypfer used 0x03)
  frame[9] = 0x03;                   // message type = GET_STATUS

  // Payload
  memcpy(&frame[10], payload, payload_len);

  // CRC8 over payload
  frame[10 + payload_len] = crc8_payload(payload, payload_len);

  // Checksum over header + payload + crc
  frame[11 + payload_len] = checksum_sum(frame.data(), 10 + payload_len + 1);

  // Send
  this->write_array(frame.data(), frame.size());
  this->flush();

  ESP_LOGD(TAG, "TX status request (len=%u)", frame.size());
  for (size_t i = 0; i < frame.size(); i++) {
    ESP_LOGD(TAG, " [%02u] 0x%02X", (unsigned)i, frame[i]);
  }
}

void MideaDehumComponent::send_set_status_() {
  // Build payload from current climate state
  uint8_t payload[11] = {
    0x41, 0x81,
    static_cast<uint8_t>(this->mode),           // Climate mode
    static_cast<uint8_t>(this->target_humidity),// Target humidity
    static_cast<uint8_t>(this->fan_mode.value_or(esphome::climate::ClimateFanMode::CLIMATE_FAN_MEDIUM)),
    0xFF,
    0x00, 0x02, 0x00, 0x00, 0x00
  };

  const uint8_t payload_len = sizeof(payload);
  const uint8_t frame_len   = 10 + payload_len + 2;

  std::vector<uint8_t> frame(frame_len);

  // Header
  frame[0] = 0xAA;
  frame[1] = 10 + payload_len + 1;
  frame[2] = 0xA1;
  frame[3] = 0x00;
  frame[4] = 0x00;
  frame[5] = 0x00;
  frame[6] = 0x00;
  frame[7] = 0x00;
  frame[8] = 0x03;
  frame[9] = 0x02;   // msgType: set status

  memcpy(&frame[10], payload, payload_len);

  frame[10 + payload_len] = crc8_payload(payload, payload_len);
  frame[11 + payload_len] = checksum_sum(frame.data(), 10 + payload_len + 1);

  this->write_array(frame.data(), frame.size());
  this->flush();

  ESP_LOGD(TAG, "TX set-status (len=%u)", frame.size());
  for (size_t i = 0; i < frame.size(); i++) {
    ESP_LOGD(TAG, " [%02u] 0x%02X", (unsigned)i, frame[i]);
  }
}

// ============ Parsing incoming =============

void MideaDehumComponent::try_parse_frame_() {
  if (rx_.size() < 12) return;  // too short

  uint8_t msgType = rx_[10];

  ESP_LOGD(TAG, "RX frame type=0x%02X len=%u", msgType, rx_.size());

  if (msgType == 0xC8) {
    // normal status
    decode_status_();
  } else if (msgType == 0x63) {
    ESP_LOGD(TAG, "Network status request (ignored for now)");
  } else {
    ESP_LOGW(TAG, "Unhandled msgType=0x%02X", msgType);
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

  this->mode = power ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;
  this->custom_preset = raw_to_preset(mode_raw);
  this->fan_mode = raw_to_fan(fan_raw);
  this->target_humidity = humi_set;
  this->current_humidity = cur;

#ifdef USE_SWITCH
  bool ionizer = (rx_[21] & 0x08) > 0;  // bit 3 = ionizer
  if (ionizer_switch_) ionizer_switch_->publish_state(ionizer);
#endif

  if (error_sensor_) error_sensor_->publish_state(err);

  ESP_LOGD(TAG, "Parsed: pwr=%d mode=%s fan=%u tset=%u cur=%u err=%u"
#ifdef USE_SWITCH
                " ionizer=%u"
#endif
                ,
         (int) power,
         this->custom_preset.has_value() ? this->custom_preset->c_str() : "(none)",
         (unsigned) fan_raw,
         (unsigned) humi_set,
         (unsigned) cur,
         (unsigned) err
#ifdef USE_SWITCH
         , (unsigned) ionizer
#endif
  );

  this->publish_state();
}

// ========== Utility functions ==========

// ===== Hypfer-style CRC8 =====
// Polynomial: 0x31 (x^8 + x^5 + x^4 + 1), init = 0x00
uint8_t MideaDehumComponent::crc8_payload(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t extract = *data++;
    for (uint8_t i = 8; i; i--) {
      uint8_t sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) {
        crc ^= 0x8C;
      }
      extract >>= 1;
    }
  }
  return crc;
}

// ===== Hypfer-style checksum =====
// Just adds everything and keeps the lowest byte
uint8_t MideaDehumComponent::checksum_sum(const uint8_t *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return static_cast<uint8_t>(sum & 0xFF);
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
