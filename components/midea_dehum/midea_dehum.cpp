#include "midea_dehum.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

/* ---------------------------------------------------------------------------
   Midea UART basics (commonly observed across Midea/Comfee/Inventor UART):

   Frame:
     [0]  0xAA                 - header
     [1]  0x23                 - message type (UART transparent)
     [2]  length               - payload length + 2 (cmd + checksum)
     [3]  cmd                  - 0x41=get status, 0x40=set status, 0xB1=get sensors
     [4..N-2] payload bytes
     [N-1] checksum            - 8-bit sum over [1..N-2] (NOT including 0xAA)
   NOTE: A few models use 0xBB...0xBD framing. If your unit does, change
         FRAME_HDR/TYPE and tail calculation below (kept isolated here).
   --------------------------------------------------------------------------- */

static constexpr uint8_t FRAME_HDR  = 0xAA;
static constexpr uint8_t FRAME_TYPE = 0x23;

// Commands we use
static constexpr uint8_t CMD_SET_STATUS   = 0x40;
static constexpr uint8_t CMD_GET_STATUS   = 0x41;
static constexpr uint8_t CMD_GET_SENSORS  = 0xB1;

/* ----------------- Offsets inside status payload (most common layout) -----
   0: flags0 (bit0 powerOn)
   1: mode (1=auto,2=cool,3=dry,5=fan,6=dry custom)
   2: fan (0..100 or 102=auto)  -- some models use steps; we normalize
   3: humidity_setpoint (30..80)
   4: error_code
   ... (others)
   Sensors (0xB1) payload:
   0: indoorHumidity (0..100)
   --------------------------------------------------------------------------- */
static constexpr uint8_t OFF_POWER        = 0;
static constexpr uint8_t OFF_MODE         = 1;
static constexpr uint8_t OFF_FAN          = 2;
static constexpr uint8_t OFF_HUMI_SET     = 3;
static constexpr uint8_t OFF_ERROR        = 4;

static constexpr uint8_t S_OFF_INDOOR_HUM = 0;

// Humidity limits
static constexpr uint8_t HUMI_MIN = 30;
static constexpr uint8_t HUMI_MAX = 80;

// Poll period
static constexpr uint32_t POLL_MS = 2000;

using climate::CLIMATE_MODE_OFF;
using climate::CLIMATE_MODE_DRY;
using climate::CLIMATE_FAN_LOW;
using climate::CLIMATE_FAN_MEDIUM;
using climate::CLIMATE_FAN_HIGH;
using climate::CLIMATE_FAN_AUTO;

/* ======================= Component lifecycle ============================ */

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "setup()");
  // Publish an initial state so HA shows the entity immediately
  this->mode = CLIMATE_MODE_DRY;
  this->fan_mode = CLIMATE_FAN_MEDIUM;
  this->current_temperature = NAN;   // not used
  this->target_temperature = desired_target_humi_;  // we map humidity to temperature slider
  this->publish_state();

  // Ask the unit for status at boot
  this->request_status_();
  this->request_sensors_();
}

void MideaDehumComponent::loop() {
  // Consume UART
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b)) break;
    this->parse_rx_byte_(b);
  }

  // Poll status/sensors periodically
  const uint32_t now = millis();
  if (now - this->last_poll_ms_ >= POLL_MS) {
    this->last_poll_ms_ = now;
    this->request_status_();
    this->request_sensors_();
  }
}

/* ======================= Climate interface ============================== */

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  // We use DRY as the base climate mode (dehumidifier)
  t.add_supported_mode(CLIMATE_MODE_OFF);
  t.add_supported_mode(CLIMATE_MODE_DRY);

  // Use fan steps Low/Medium/High (no Auto by default)
  t.add_supported_fan_mode(CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(CLIMATE_FAN_MEDIUM);
  t.add_supported_fan_mode(CLIMATE_FAN_HIGH);

  // Expose custom presets as “modes” for the dehumidifier features
  t.set_supported_custom_presets({
      PRESET_SMART, PRESET_SETPOINT, PRESET_CONTINUOUS, PRESET_CLOTHES_DRY,
  });

  // We don’t use temperatures; map humidity setpoint (30..80) to target_temperature
  t.set_supports_current_temperature(false);
  t.set_visual_min_temperature(HUMI_MIN);
  t.set_visual_max_temperature(HUMI_MAX);
  t.set_visual_temperature_step(1.0f);

  return t;
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  bool need_send = false;

  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
    if (m == CLIMATE_MODE_OFF) {
      this->desired_power_ = false;
      this->mode = CLIMATE_MODE_OFF;
      need_send = true;
    } else if (m == CLIMATE_MODE_DRY) {
      this->desired_power_ = true;
      this->mode = CLIMATE_MODE_DRY;
      need_send = true;
    }
  }

  if (call.get_fan_mode().has_value()) {
    this->desired_fan_ = *call.get_fan_mode();
    this->fan_mode = this->desired_fan_;
    need_send = true;
  }

  if (call.get_target_temperature().has_value()) {
    // Map HA target_temperature to humidity setpoint
    int h = static_cast<int>(std::lroundf(*call.get_target_temperature()));
    h = std::max<int>(HUMI_MIN, std::min<int>(HUMI_MAX, h));
    this->desired_target_humi_ = static_cast<uint8_t>(h);
    this->target_temperature = h;
    need_send = true;
  }

  if (call.get_preset().has_value()) {
    this->desired_preset_ = *call.get_preset();
    this->set_custom_preset_(this->desired_preset_);
    need_send = true;
  }

  if (need_send) {
    this->publish_state();
    this->send_set_status_();
  }
}

/* ======================= UART protocol ================================= */

// Build command frame: 0xAA 0x23 len cmd payload... checksum
std::vector<uint8_t> MideaDehumComponent::build_cmd_(uint8_t cmd, const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> out;
  out.reserve(4 + payload.size());
  out.push_back(FRAME_HDR);
  out.push_back(FRAME_TYPE);
  // len = cmd + payload + checksum
  uint8_t len = static_cast<uint8_t>(1 + payload.size() + 1);
  out.push_back(len);
  out.push_back(cmd);
  out.insert(out.end(), payload.begin(), payload.end());

  // checksum is sum over bytes [1..end-1] (exclude header)
  uint8_t sum = checksum8_(out, 1, out.size());
  out.push_back(sum);
  return out;
}

uint8_t MideaDehumComponent::checksum8_(const std::vector<uint8_t> &bytes, size_t from, size_t to) {
  uint32_t s = 0;
  for (size_t i = from; i < to; i++) s += bytes[i];
  return static_cast<uint8_t>(s & 0xFF);
}

void MideaDehumComponent::push_tx_(const std::vector<uint8_t> &frame) {
  this->write_array(frame);
  this->flush();
  ESP_LOGD(TAG, "TX %u bytes", (unsigned)frame.size());
}

void MideaDehumComponent::request_status_() {
  auto f = build_cmd_(CMD_GET_STATUS, {});
  push_tx_(f);
}

void MideaDehumComponent::request_sensors_() {
  auto f = build_cmd_(CMD_GET_SENSORS, {});
  push_tx_(f);
}

void MideaDehumComponent::send_set_status_() {
  // payload according to common 0x40 layout we use:
  // [0] flags0 (bit0 power)
  // [1] mode
  // [2] fan percent (0..100, 102=auto) — we map L/M/H to 25/60/100
  // [3] humidity setpoint (30..80)
  // [4] reserved 0
  std::vector<uint8_t> pl;
  pl.reserve(5);
  uint8_t flags0 = (this->desired_power_ ? 0x01 : 0x00);
  pl.push_back(flags0);
  pl.push_back(map_preset_to_mode_(this->desired_preset_));
  pl.push_back(map_fan_to_percent_(this->desired_fan_));
  pl.push_back(this->desired_target_humi_);
  pl.push_back(0x00);

  auto f = build_cmd_(CMD_SET_STATUS, pl);
  push_tx_(f);
}

/* ======================= RX state machine =============================== */

void MideaDehumComponent::parse_rx_byte_(uint8_t b) {
  rx_.push_back(b);

  // Minimal frame sanity: head, type, len
  if (rx_.size() == 1 && rx_[0] != FRAME_HDR) {
    rx_.clear();
    return;
  }
  if (rx_.size() >= 3) {
    const size_t total = static_cast<size_t>(rx_[2]) + 3; // HDR(1) + TYPE(1) + LEN(1) + payload + checksum
    if (rx_.size() == total) {
      try_parse_frame_();
      rx_.clear();
    } else if (rx_.size() > total || total < 5 || total > 128) {
      // bad frame
      rx_.clear();
    }
  }
}

void MideaDehumComponent::try_parse_frame_() {
  if (rx_.size() < 5) return;
  if (rx_[0] != FRAME_HDR || rx_[1] != FRAME_TYPE) return;

  const uint8_t len = rx_[2];
  if (len < 2) return;  // at least cmd+checksum

  const size_t end = rx_.size();
  const uint8_t sum = checksum8_(rx_, 1, end - 1);
  if (sum != rx_[end - 1]) {
    ESP_LOGW(TAG, "Checksum mismatch");
    return;
  }

  const uint8_t cmd = rx_[3];
  const size_t payload_len = static_cast<size_t>(len - 2);  // without cmd+checksum
  std::vector<uint8_t> payload;
  payload.reserve(payload_len);
  if (payload_len > 0) {
    payload.insert(payload.end(), rx_.begin() + 4, rx_.begin() + 4 + payload_len);
  }

  switch (cmd) {
    case CMD_GET_STATUS:
      decode_status_(payload);
      break;
    case CMD_GET_SENSORS:
      decode_sensors_(payload);
      break;
    case CMD_SET_STATUS:
      // Some models echo a short ACK or a full state, handle both:
      if (!payload.empty()) decode_status_(payload);
      break;
    default:
      ESP_LOGD(TAG, "RX unknown cmd=0x%02X len=%u", cmd, (unsigned)payload_len);
      break;
  }
}

/* ======================= Decoders ====================================== */

void MideaDehumComponent::decode_status_(const std::vector<uint8_t> &p) {
  if (p.size() < 5) return;

  bool power = (p[OFF_POWER] & 0x01) != 0;
  uint8_t mode = p[OFF_MODE];
  uint8_t fan_pct = p[OFF_FAN];
  uint8_t humi_sp = p[OFF_HUMI_SET];
  uint8_t err = p[OFF_ERROR];

  // publish error (numeric)
  if (this->error_sensor_) this->error_sensor_->publish_state((float) err);

  // Update climate view
  this->mode = power ? CLIMATE_MODE_DRY : CLIMATE_MODE_OFF;
  this->fan_mode = map_percent_to_fan_(fan_pct);

  // We map humidity setpoint to target_temperature for the HA slider
  if (humi_sp >= HUMI_MIN && humi_sp <= HUMI_MAX) {
    this->target_temperature = humi_sp;
    this->desired_target_humi_ = humi_sp;
  }

  // Map protocol mode to our presets
  this->set_custom_preset_(map_mode_to_preset_(mode));

  this->publish_state();
}

void MideaDehumComponent::decode_sensors_(const std::vector<uint8_t> &p) {
  if (p.size() < 1) return;
  const uint8_t hum = p[S_OFF_INDOOR_HUM];
  // There is no native humidity field in ESPHome Climate. We keep it as “current_temperature” for UI feedback.
  this->current_temperature = hum;  // displayed number only
  this->publish_state();
}

/* ======================= Mapping utils ================================= */

uint8_t MideaDehumComponent::map_preset_to_mode_(const std::string &preset) const {
  // UART "mode" values (common Midea):
  // 1=Auto, 3=Dry, 5=Fan, 6=Dry(custom)
  if (preset == PRESET_SMART)       return 1; // Auto (smart)
  if (preset == PRESET_SETPOINT)    return 3; // Dry with setpoint
  if (preset == PRESET_CONTINUOUS)  return 6; // Dry custom (continuous)
  if (preset == PRESET_CLOTHES_DRY) return 6; // Same base mode; unit may apply its own curve
  // fallback
  return 3;
}

std::string MideaDehumComponent::map_mode_to_preset_(uint8_t mode) const {
  switch (mode) {
    case 1: return PRESET_SMART;
    case 3: return PRESET_SETPOINT;
    case 6: return PRESET_CONTINUOUS;
    case 5: return PRESET_SETPOINT; // fan-only -> present as setpoint mode fallback
    default: return PRESET_SETPOINT;
  }
}

uint8_t MideaDehumComponent::map_fan_to_percent_(climate::ClimateFanMode fan) const {
  switch (fan) {
    case CLIMATE_FAN_LOW:    return 25;
    case CLIMATE_FAN_MEDIUM: return 60;
    case CLIMATE_FAN_HIGH:   return 100;
    default:                 return 60;
  }
}

climate::ClimateFanMode MideaDehumComponent::map_percent_to_fan_(uint8_t pct) const {
  if (pct <= 35) return CLIMATE_FAN_LOW;
  if (pct <= 80) return CLIMATE_FAN_MEDIUM;
  return CLIMATE_FAN_HIGH;
}

}  // namespace midea_dehum
}  // namespace esphome
