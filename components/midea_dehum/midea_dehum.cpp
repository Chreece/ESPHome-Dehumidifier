#include "midea_dehum.h"
#include "esphome/core/log.h"

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";
MideaDehumComponent::~MideaDehumComponent() = default;

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

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent *uart) {
  this->set_uart_parent(uart);   // ← correct API
  this->uart_ = uart;
  ESP_LOGI(TAG, "UART parent set and pointer stored.");
}

void MideaDehumComponent::setup() {
  // Set initial climate states
  this->mode = climate::CLIMATE_MODE_OFF;
  this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  this->target_temperature = desired_target_humi_;  // Your desired target humidity variable
  this->custom_preset = std::string(PRESET_SMART);

  // Publish initial state to Home Assistant via ESPHome API
  this->publish_state();

  // Initialize error sensor to 0 (no error)
  if (this->error_sensor_)
    this->error_sensor_->publish_state(0);

  // Send the first status request to the device
  this->request_status_();
}

void MideaDehumComponent::loop() {
  if (!uart_) {
    ESP_LOGW("midea_dehum", "UART not initialized!");
    delay(1000);
    return;
  }
  static uint32_t last_request = 0;
  uint32_t now = millis();

  if (now - last_request > 10000) {  // every 10 seconds
    last_request = now;
    this->request_status_();
  }
  // Read available bytes one by one
  while (uart_->available()) {
    uint8_t b;
    if (uart_->read_byte(&b)) {
      ESP_LOGD(TAG, "RX byte: 0x%02X", b);
      rx_.push_back(b);
    } else {
      ESP_LOGW(TAG, "UART read_byte failed");
      break;
    }
  }

  // Try parsing complete frames from rx_
  try_parse_frame_();

  // Prevent buffer overflow
  if (rx_.size() > 1024) {
    ESP_LOGW("midea_dehum", "RX buffer too large, clearing");
    rx_.clear();
  }

  delay(1);  // Yield to watchdog
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
    } else if (this->mode == climate::CLIMATE_MODE_DRY) {
      this->desired_power_ = true;
    }
    changed = true;
  }

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
    updateSetStatus(
      this->desired_power_,
      this->desired_preset_,
      this->desired_fan_,
      this->desired_target_humi_
    );

    this->send_set_status_();

    this->publish_state();
  }
}

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

void MideaDehumComponent::updateSetStatus(bool power_on, const std::string &preset,
                                          climate::ClimateFanMode fan, uint8_t humidity_setpoint) {
  memset(tx_buf_, 0, sizeof(tx_buf_));
  tx_buf_[0] = 0x48;                           // Magic
  tx_buf_[1] = power_on ? 0x01 : 0x00;         // Power
  tx_buf_[2] = preset_to_raw(preset);          // 0x00 smart, 0x01 setpoint, 0x02 continuous, 0x03 clothes_dry
  tx_buf_[3] = fan_to_raw(fan);                // 1/2/3 (see fix #3)
  tx_buf_[7] = (uint8_t) humidity_setpoint;    // target humidity
  ESP_LOGD(TAG, "Prepared set status: pwr=%d preset=%s raw=0x%02X fanRaw=%u target=%u",
           power_on, preset.c_str(), tx_buf_[2], tx_buf_[3], (unsigned)tx_buf_[7]);
}

void MideaDehumComponent::request_status_() {
  // Full Hypfer "get status" command (21 bytes)
  static const uint8_t payload[21] = {
    0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03
  };

  const uint8_t payload_len = sizeof(payload);
  const uint8_t frame_len   = 10 + payload_len + 2;

  std::vector<uint8_t> frame(frame_len);

  // --- Header ---
  frame[0] = 0xAA;                     // sync
  frame[1] = 10 + payload_len + 1;     // length (header + payload + crc)
  frame[2] = 0xA1;                     // appliance type
  frame[3] = 0x00;
  frame[4] = 0x00;
  frame[5] = 0x00;
  frame[6] = 0x00;
  frame[7] = 0x00;
  frame[8] = 0x03;                     // agreement version
  frame[9] = 0x03;                     // msgType = GET_STATUS

  // --- Payload ---
  memcpy(&frame[10], payload, payload_len);

  // --- CRC8 over payload only ---
  frame[10 + payload_len] = crc8_payload(payload, payload_len);

  // --- Checksum over header + payload + CRC ---
  frame[11 + payload_len] = checksum_sum(frame.data(), 10 + payload_len + 1);

  // --- Send ---
  this->write_array(frame.data(), frame.size());
  this->flush();

  ESP_LOGD(TAG, "TX status request (len=%u)", frame.size());
  for (size_t i = 0; i < frame.size(); i++) {
    ESP_LOGD(TAG, " [%02u] 0x%02X", (unsigned)i, frame[i]);
  }
}

void MideaDehumComponent::send_set_status_() {
  constexpr uint8_t payload_len = 25;  // Hypfer used 25
  const uint8_t frame_len = 10 + payload_len + 2;
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
  frame[9] = 0x02; // SET_STATUS

  // Payload from tx_buf_ prepared in updateSetStatus()
  memcpy(&frame[10], tx_buf_, payload_len);

  // CRC payload + checksum header+payload+crc
  frame[10 + payload_len] = crc8_payload(tx_buf_, payload_len);
  frame[11 + payload_len] = checksum_sum(frame.data(), 10 + payload_len + 1);

  this->write_array(frame.data(), frame.size());
  this->flush();

  ESP_LOGD(TAG, "TX set-status (len=%u)", frame.size());
}

void MideaDehumComponent::try_parse_frame_() {
  if (rx_.size() < 2) return;

  size_t expected = calculate_frame_length(rx_);
  if (expected == 0 || rx_.size() < expected) return;

  uint8_t msgType = rx_[9];
  ESP_LOGD(TAG, "Got frame msgType=0x%02X len=%u", msgType, (unsigned)expected);

  if (msgType == 0xC8) {
    ESP_LOGI(TAG, "✅ Received STATUS (0xC8)");
    std::vector<uint8_t> frame(rx_.begin(), rx_.begin() + expected);
    decode_status_(frame);
  } else if (msgType == 0x63) {
    ESP_LOGI(TAG, "NET status (0x63) ignored for now");
  } else {
    ESP_LOGW(TAG, "Unhandled msgType=0x%02X", msgType);
  }

  rx_.erase(rx_.begin(), rx_.begin() + expected);
}

size_t MideaDehumComponent::calculate_frame_length(const std::vector<uint8_t> &buf) {
  if (buf.size() < 2) return 0;
  return buf[1];
}

void MideaDehumComponent::decode_status_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 32) return;

  bool power = (frame[11] & 0x01) > 0;
  uint8_t mode_raw = frame[12] & 0x0F;
  uint8_t fan_raw  = frame[13] & 0x7F;

  uint8_t target = frame[17] >= 100 ? 99 : frame[17];
  uint8_t cur    = frame[26];
  uint8_t err    = frame[31];

  // Map Midea fields to ESPHome entities
  this->mode = power ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;
  this->custom_preset = raw_to_preset(mode_raw);
  this->fan_mode = raw_to_fan(fan_raw);

  // Use ESPHome's temperature fields for humidity representation
  this->target_temperature = target;
  this->current_temperature = cur;

  if (this->error_sensor_) {
    this->error_sensor_->publish_state(err);
  }

  ESP_LOGI(TAG, "Parsed: Power=%s Preset=%s FanRaw=0x%02X Target=%u%% Current=%u%% Error=%u",
           power ? "ON" : "OFF",
           this->custom_preset.has_value() ? this->custom_preset->c_str() : "(none)",
           fan_raw, target, cur, err);

  this->publish_state();
}

// ========== Utility functions ==========
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
    default: return climate::CLIMATE_FAN_MEDIUM; // 2
  }
}
uint8_t MideaDehumComponent::fan_to_raw(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW:  return 1;
    case climate::CLIMATE_FAN_HIGH: return 3;
    default:                        return 2;
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
