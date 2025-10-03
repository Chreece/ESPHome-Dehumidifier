#include "midea_dehum.h"
#include "esphome/core/log.h"
#include <cstring>
#include <algorithm>
#include <set>

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

// ======= Protocol constants matching your Arduino sketch =======

// Header: 10 bytes then payload, then CRC8(payload), then checksum(sum over header+payload+CRC8)
static constexpr uint8_t HDR_SYNC = 0xAA;

// msgType values (index 9 of header on TX, and rx[10] when reading full frame):
//  - 0xC8 : device status frame we parse
//  - 0x63 : module asks for network status (we reply with msgType 0x0D)
//  - 0x03 : "getStatus" request we send
//  - 0x02 : "setStatus" request we send
static constexpr uint8_t MSG_GET_STATUS      = 0x03;
static constexpr uint8_t MSG_SET_STATUS      = 0x02;
static constexpr uint8_t MSG_NET_STATUS_REQ  = 0x63;
static constexpr uint8_t MSG_NET_STATUS_RSP  = 0x0D;
static constexpr uint8_t MSG_DEVICE_STATUS   = 0xC8;

// Agreement version used by the sketch
static constexpr uint8_t AGREEMENT_VERSION   = 0x03;

// GetStatus payload from sketch (21 bytes)
static const uint8_t GET_STATUS_PAYLOAD[21] = {
  0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03
};

// Our dehumidifier limits (same as earlier)
static constexpr uint8_t HUMI_MIN = 30;
static constexpr uint8_t HUMI_MAX = 80;

// ======================= Helpers ===============================

static uint8_t crc8_payload(const uint8_t *data, size_t len) {
  // Many Midea payloads use CRC8 poly 0x31 init 0x00 (Dallas). This matches most modules.
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;  // poly 0x31 reflected
      inbyte >>= 1;
    }
  }
  return crc;
}

static uint8_t checksum_sum(const uint8_t *data, size_t len) {
  uint32_t s = 0;
  for (size_t i = 0; i < len; i++) s += data[i];
  return static_cast<uint8_t>(s & 0xFF);
}

// Map raw fan <-> ESPHome enum (your sketch uses 1/2/3 for low/med/high)
static climate::ClimateFanMode raw_to_fan(uint8_t raw) {
  switch (raw) {
    case 1: return climate::CLIMATE_FAN_LOW;
    case 3: return climate::CLIMATE_FAN_HIGH;
    default: return climate::CLIMATE_FAN_MEDIUM;
  }
}
static uint8_t fan_to_raw(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW: return 1;
    case climate::CLIMATE_FAN_HIGH: return 3;
    default: return 2;
  }
}

// Map protocol mode nibble <-> preset strings (as in your sketch)
static std::string mode_to_preset(uint8_t m) {
  switch (m & 0x0F) {
    case 0x01: return PRESET_SETPOINT;
    case 0x02: return PRESET_CONTINUOUS;
    case 0x03: return PRESET_CLOTHES_DRY;
    default:   return PRESET_SMART;
  }
}
static uint8_t preset_to_mode(const std::string &p) {
  if (p == PRESET_SETPOINT)    return 0x01;
  if (p == PRESET_CONTINUOUS)  return 0x02;
  if (p == PRESET_CLOTHES_DRY) return 0x03;
  return 0x00; // smart
}

// ======================= Component lifecycle ===================

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "setup() starting");

  // Make entity visible immediately
  this->mode = climate::CLIMATE_MODE_OFF;
  this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  this->target_temperature = 50;   // target humidity mapped to temperature slider
  this->current_temperature = NAN; // unknown yet
  this->preset = PRESET_SMART;
  this->publish_state();

  if (this->error_sensor_) this->error_sensor_->publish_state(0);

  // Ask for status at boot
  this->request_status_();
}

void MideaDehumComponent::loop() {
  // Read bytes and assemble frames
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b)) break;
    this->parse_rx_byte_(b);
  }

  // Optionally, you can poll periodically for status here if your unit is chatty enough
  // (left out to keep traffic minimal; press buttons on OEM panel to see frames).
}

// ======================= Climate interface ======================

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;

  t.set_supports_current_temperature(true);  // we display current humidity here
  // target humidity via temp slider
  t.set_visual_min_temperature(HUMI_MIN);
  t.set_visual_max_temperature(HUMI_MAX);
  t.set_visual_temperature_step(1.0f);

  t.set_supported_modes({ climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO });
  t.set_supported_fan_modes({
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH
  });

  // Custom string presets for your four modes
  t.set_supported_custom_presets(std::set<std::string>{
    PRESET_SMART, PRESET_SETPOINT, PRESET_CONTINUOUS, PRESET_CLOTHES_DRY
  });

  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  bool changed = false;

  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    // Protocol uses power flag; treat AUTO as "on"
    changed = true;
  }

  if (call.get_fan_mode().has_value()) {
    this->fan_mode = *call.get_fan_mode();
    changed = true;
  }

  if (call.get_target_temperature().has_value()) {
    int h = std::lroundf(*call.get_target_temperature());
    h = std::max<int>(HUMI_MIN, std::min<int>(HUMI_MAX, h));
    this->target_temperature = h;
    changed = true;
  }

  if (call.get_preset().has_value()) {
    this->preset = *call.get_preset();
    changed = true;
  }

  if (changed) {
    this->send_set_status_();
    this->publish_state();
  }
}

// ======================= UART TX (builder) ======================

void MideaDehumComponent::build_header_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength) {
  // Matches your writeHeader()
  // [0]=AA, [1]=10+payload+1, [2]=0xA1, [9]=msgType
  this->header_[0] = HDR_SYNC;                // 0xAA
  this->header_[1] = static_cast<uint8_t>(10 + payloadLength + 1); // length byte per sketch
  this->header_[2] = 0xA1;                    // Appliance type
  this->header_[3] = 0x00;
  this->header_[4] = 0x00;
  this->header_[5] = 0x00;
  this->header_[6] = 0x00;                    // MsgId
  this->header_[7] = 0x00;                    // ProtocolVersion
  this->header_[8] = agreementVersion;
  this->header_[9] = msgType;
}

void MideaDehumComponent::send_message_(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, const uint8_t *payload) {
  // header (10) + payload (N) + crc8 (1) + checksum (1)
  size_t total = 10 + payloadLength + 2;
  if (total > sizeof(this->tx_buf_)) {
    ESP_LOGW(TAG, "TX too large (%u)", (unsigned) total);
    return;
  }

  this->build_header_(msgType, agreementVersion, payloadLength);
  memcpy(this->tx_buf_, this->header_, 10);
  if (payloadLength > 0)
    memcpy(this->tx_buf_ + 10, payload, payloadLength);

  // CRC8 over payload
  this->tx_buf_[10 + payloadLength] = crc8_payload(this->tx_buf_ + 10, payloadLength);
  // Checksum over header + payload + CRC8
  this->tx_buf_[10 + payloadLength + 1] = checksum_sum(this->tx_buf_, 10 + payloadLength + 1);

  this->write_array(this->tx_buf_, total);
  this->flush();
  ESP_LOGD(TAG, "TX %u bytes (msgType=0x%02X, len=%u)", (unsigned) total, msgType, (unsigned) payloadLength);
}

void MideaDehumComponent::request_status_() {
  this->send_message_(MSG_GET_STATUS, AGREEMENT_VERSION, 21, GET_STATUS_PAYLOAD);
}

void MideaDehumComponent::send_set_status_() {
  // Build the 25-byte setStatusCommand like your updateSetStatus()
  uint8_t pl[25];
  memset(pl, 0, sizeof(pl));
  pl[0] = 0x48; // magic
  // power derived from climate mode
  bool power_on = (this->mode != climate::CLIMATE_MODE_OFF);
  pl[1] = power_on ? 0x01 : 0x00;
  // mode preset nibble
  pl[2] = preset_to_mode(this->preset) & 0x0F;
  // fan
  pl[3] = fan_to_raw(this->fan_mode);
  // target humidity (sketch uses byte 7)
  {
    int th = std::lroundf(this->target_temperature);
    th = std::max<int>(HUMI_MIN, std::min<int>(HUMI_MAX, th));
    pl[7] = static_cast<uint8_t>(th);
  }
  this->send_message_(MSG_SET_STATUS, AGREEMENT_VERSION, sizeof(pl), pl);
}

// Send network status if asked (optional, but handy)
static void fill_network_status(uint8_t out[20], bool connected) {
  memset(out, 0, 20);
  out[0] = 0x01;             // WiFi module
  out[1] = 0x01;             // client mode
  out[2] = 0x04;             // strong WiFi
  // 3..6 IP reversed (127.0.0.1)
  out[3] = 1; out[4] = 0; out[5] = 0; out[6] = 127;
  out[7] = 0xFF;             // RF unsupported
  out[8] = connected ? 0x00 : 0x01; // router status
  out[9] = connected ? 0x00 : 0x01; // cloud status
  // 10..19 reserved
}

// ======================= UART RX (assembler) ====================

void MideaDehumComponent::parse_rx_byte_(uint8_t b) {
  this->rx_.push_back(b);

  // minimal sanity: must start with 0xAA
  if (this->rx_.size() == 1 && this->rx_[0] != HDR_SYNC) {
    this->rx_.clear();
    return;
  }

  // once we have the length byte, we can know total
  if (this->rx_.size() >= 2) {
    uint8_t len_byte = this->rx_[1];  // 10 + payload + 1
    // total frame bytes = header(10) + payload + CRC8(1) + SUM(1)
    size_t total = 10 + (len_byte - 11) + 2; // because len_byte = 10 + payload + 1 => payload = len-11
    if (len_byte < 11 || total > 256) {
      // invalid
      this->rx_.clear();
      return;
    }
    if (this->rx_.size() == total) {
      this->try_parse_frame_();
      this->rx_.clear();
    } else if (this->rx_.size() > total) {
      // overshoot -> resync
      this->rx_.clear();
    }
  }
}

void MideaDehumComponent::try_parse_frame_() {
  if (this->rx_.size() < 12) return;
  // verify checksum
  const size_t n = this->rx_.size();
  const uint8_t calc_sum = checksum_sum(this->rx_.data(), n - 1);
  if (calc_sum != this->rx_[n - 1]) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }
  // verify CRC8 over payload
  uint8_t len_byte = this->rx_[1];
  size_t payload_len = static_cast<size_t>(len_byte - 11);
  const uint8_t calc_crc = crc8_payload(&this->rx_[10], payload_len);
  if (calc_crc != this->rx_[10 + payload_len]) {
    ESP_LOGW(TAG, "CRC8 mismatch");
    return;
  }

  // msgType is at header[9] on TX; in RX they checked serialRxBuf[10]
  // but since header is 10 bytes, serialRxBuf[10] is actually the first payload byte OR the
  // "message class" byte depending on the module's formatting. The original sketch tests rx[10].
  const uint8_t indicator = this->rx_[10];

  if (indicator == MSG_DEVICE_STATUS) {
    // parse with exact absolute offsets from your sketch
    this->decode_status_(/*payload*/ std::vector<uint8_t>{}); // we'll read from rx_ directly
  } else if (indicator == MSG_NET_STATUS_REQ) {
    // reply with network status (optional); keep HA happy
    uint8_t net[20];
    fill_network_status(net, true);
    this->send_message_(MSG_NET_STATUS_RSP, AGREEMENT_VERSION, 20, net);
  } else {
    ESP_LOGD(TAG, "RX frame indicator=0x%02X, len=%u", indicator, (unsigned) payload_len);
  }
}

// ======================= Decoders ===============================

void MideaDehumComponent::decode_status_(const std::vector<uint8_t> & /*unused*/) {
  // EXACT offsets from your parseState()
  if (this->rx_.size() <= 32) return;

  bool power_on        = (this->rx_[11] & 0x01) > 0;
  uint8_t mode_raw     = (this->rx_[12] & 0x0F);
  uint8_t fan_raw      = (this->rx_[13] & 0x7F);
  uint8_t humi_set_raw = this->rx_[17] >= 100 ? 99 : this->rx_[17];
  uint8_t cur_humi     = this->rx_[26];
  uint8_t err          = this->rx_[31];

  // Update internal state
  this->mode = power_on ? climate::CLIMATE_MODE_AUTO : climate::CLIMATE_MODE_OFF;
  this->preset = mode_to_preset(mode_raw);
  this->fan_mode = raw_to_fan(fan_raw);
  this->target_temperature = std::min<int>(HUMI_MAX, std::max<int>(HUMI_MIN, humi_set_raw));
  this->current_temperature = cur_humi; // displayed as number on HA card

  if (this->error_sensor_) this->error_sensor_->publish_state(err);

  ESP_LOGD(TAG, "Decoded: power=%d mode=0x%02X fan=%u target=%u current=%u err=%u",
           power_on, mode_raw, (unsigned) fan_raw, (unsigned) humi_set_raw,
           (unsigned) cur_humi, (unsigned) err);

  this->publish_state();
}

void MideaDehumComponent::decode_sensors_(const std::vector<uint8_t> & /*payload*/) {
  // Not used in this protocol flavor; current humidity is in rx_[26] already.
}

// ======================= Compatibility shims =====================

uint8_t MideaDehumComponent::checksum8_(const std::vector<uint8_t> &bytes, size_t from, size_t to) {
  uint32_t s = 0;
  for (size_t i = from; i < to; i++) s += bytes[i];
  return static_cast<uint8_t>(s & 0xFF);
}

std::vector<uint8_t> MideaDehumComponent::build_cmd_(uint8_t msgType, const std::vector<uint8_t> &payload) {
  // Build a full frame per your sendMessage(), then return it (for push_tx_)
  const size_t payload_len = payload.size();
  std::vector<uint8_t> out;
  out.resize(10 + payload_len + 2, 0x00);

  // header
  out[0] = HDR_SYNC;
  out[1] = static_cast<uint8_t>(10 + payload_len + 1);
  out[2] = 0xA1;
  out[3] = 0x00;
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = 0x00;
  out[8] = AGREEMENT_VERSION;
  out[9] = msgType;

  // payload
  if (payload_len) memcpy(&out[10], payload.data(), payload_len);

  // CRC8 over payload
  out[10 + payload_len] = crc8_payload(&out[10], payload_len);
  // checksum (sum) over all prior bytes
  out[10 + payload_len + 1] = checksum_sum(out.data(), 10 + payload_len + 1);

  return out;
}

void MideaDehumComponent::push_tx_(const std::vector<uint8_t> &frame) {
  this->write_array(frame);
  this->flush();
  ESP_LOGD(TAG, "TX %u bytes (push)", (unsigned) frame.size());
}

// ======================= Unused in this flavor ===================

void MideaDehumComponent::request_sensors_() {
  // Not used by this protocol (current humidity already in status frame)
}

uint8_t MideaDehumComponent::map_preset_to_mode_(const std::string &preset) const {
  return preset_to_mode(preset);
}
std::string MideaDehumComponent::map_mode_to_preset_(uint8_t mode) const {
  return mode_to_preset(mode);
}
uint8_t MideaDehumComponent::map_fan_to_percent_(climate::ClimateFanMode /*fan*/) const {
  // not used in this protocol
  return 0;
}
climate::ClimateFanMode MideaDehumComponent::map_percent_to_fan_(uint8_t /*pct*/) const {
  // not used in this protocol
  return climate::CLIMATE_FAN_MEDIUM;
}

}  // namespace midea_dehum
}  // namespace esphome
