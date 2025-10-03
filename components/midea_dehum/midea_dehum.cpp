#include "midea_dehum.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

// ------------------------------------------------------------------
// Simplified UART protocol (aligned to Hypfer's project layout):
//   Frame: [0xAA][CMD][DATA][CHK], CHK = sum(all previous bytes) mod 256
// Commands used here:
//   0x01 POWER     : DATA 0x00=OFF, 0x01=ON
//   0x02 MODE      : DATA 0x00=SMART, 0x01=SETPOINT, 0x02=CONTINUOUS, 0x03=CLOTHES_DRY
//   0x03 FAN       : DATA 0x01=LOW, 0x02=MEDIUM, 0x03=HIGH
//   0x04 TARGET RH : DATA 30..80
//   0x08 CURR RH   : DATA 0..100
//   0x0A ERROR     : DATA error code
// ------------------------------------------------------------------

static constexpr uint8_t HDR        = 0xAA;
static constexpr uint8_t CMD_POWER  = 0x01;
static constexpr uint8_t CMD_MODE   = 0x02;
static constexpr uint8_t CMD_FAN    = 0x03;
static constexpr uint8_t CMD_TARGET = 0x04;
static constexpr uint8_t CMD_CURRH  = 0x08;
static constexpr uint8_t CMD_ERROR  = 0x0A;

// ---------------- Life-cycle ----------------

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier initialized (Climate)");
  // Default visible state so HA shows entity immediately
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_humidity = 50;
  this->current_humidity = NAN;
  this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  // We expose custom "modes" via mapping to standard climate modes:
  //   SMART        -> CLIMATE_MODE_AUTO
  //   SETPOINT     -> CLIMATE_MODE_DRY
  //   CONTINUOUS   -> CLIMATE_MODE_FAN_ONLY
  //   CLOTHES_DRY  -> CLIMATE_MODE_HEAT (UI label is "Heat", but acts as special dry mode)
  this->publish_state();
}

void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t b = this->read();
    rx_.push_back(b);

    // Resync to header if noise arrives
    while (!rx_.empty() && rx_.front() != HDR) rx_.erase(rx_.begin());

    // Minimal fixed 4-byte frame
    while (rx_.size() >= 4) {
      std::vector<uint8_t> f(rx_.begin(), rx_.begin() + 4);
      parse_frame_(f);
      rx_.erase(rx_.begin(), rx_.begin() + 4);
    }

    // keep buffer bounded
    if (rx_.size() > 64) rx_.clear();
  }
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;

  // Humidity (current + target)
  t.set_supports_current_humidity(true);
  t.set_supports_target_humidity(true);
  t.set_visual_min_humidity(30);
  t.set_visual_max_humidity(80);

  // Fan speeds
  t.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);

  // Modes (mapped as described in setup())
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_AUTO);      // "smart"
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);       // "setpoint"
  t.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);  // "continuous"
  t.add_supported_mode(climate::CLIMATE_MODE_HEAT);      // "clothes_dry" (labelled Heat in UI)

  return t;
}

// ---------------- Control ----------------

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  bool changed = false;

  // Mode selection
  if (call.get_mode().has_value()) {
    auto m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF) {
      send_cmd3_(CMD_POWER, 0x00);
      this->mode = climate::CLIMATE_MODE_OFF;
      changed = true;
    } else {
      // Ensure powered ON for any non-off mode
      send_cmd3_(CMD_POWER, 0x01);
      // Map to protocol mode byte
      uint8_t pm = encode_mode_(m, climate::CLIMATE_PRESET_NONE);
      send_cmd3_(CMD_MODE, pm);
      this->mode = m;
      changed = true;
    }
  }

  // Target humidity
  if (call.get_target_humidity().has_value()) {
    int h = *call.get_target_humidity();
    if (h < 30) h = 30;
    if (h > 80) h = 80;
    send_cmd3_(CMD_TARGET, static_cast<uint8_t>(h));
    this->target_humidity = h;
    changed = true;
  }

  // Fan speed
  if (call.get_fan_mode().has_value()) {
    auto f = *call.get_fan_mode();
    uint8_t pf = encode_fan_(f);
    if (pf != 0x00) {  // 0x00 means unsupported
      send_cmd3_(CMD_FAN, pf);
      this->fan_mode = f;
      changed = true;
    } else {
      ESP_LOGW(TAG, "Unsupported fan mode requested");
    }
  }

  if (changed) this->publish_state();
}

// ---------------- Parsing ----------------

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &f) {
  if (f.size() < 4 || f[0] != HDR) return;

  uint8_t calc = checksum_(std::vector<uint8_t>(f.begin(), f.end() - 1));
  if (calc != f.back()) {
    ESP_LOGW(TAG, "Checksum mismatch (got 0x%02X expected 0x%02X)", f.back(), calc);
    return;
  }

  uint8_t cmd  = f[1];
  uint8_t data = f[2];
  bool pub = false;

  switch (cmd) {
    case CMD_POWER:
      this->mode = (data == 0x00) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_AUTO;
      pub = true;
      break;

    case CMD_MODE:
      this->mode = map_mode_for_ui_(data);
      pub = true;
      break;

    case CMD_FAN:
      this->fan_mode = map_fan_for_ui_(data);
      pub = true;
      break;

    case CMD_TARGET:
      this->target_humidity = data;
      pub = true;
      break;

    case CMD_CURRH:
      this->current_humidity = data;
      pub = true;
      break;

    case CMD_ERROR:
      if (this->error_sensor_ != nullptr) this->error_sensor_->publish_state(data);
      break;

    default:
      ESP_LOGV(TAG, "Unhandled cmd=0x%02X data=0x%02X", cmd, data);
      break;
  }

  if (pub) this->publish_state();
}

// ---------------- Helpers ----------------

void MideaDehumComponent::send_cmd3_(uint8_t cmd, uint8_t data) {
  std::vector<uint8_t> out;
  out.reserve(4);
  out.push_back(HDR);
  out.push_back(cmd);
  out.push_back(data);
  out.push_back(checksum_(out));
  for (auto b : out) this->write_byte(b);
  ESP_LOGV(TAG, "TX: %02X %02X %02X %02X", out[0], out[1], out[2], out[3]);
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
  uint8_t s = 0;
  for (auto b : bytes) s += b;
  return s;
}

// Mode mapping
uint8_t MideaDehumComponent::encode_mode_(climate::ClimateMode m, climate::ClimatePreset /*unused*/) {
  // Protocol: 0x00 SMART, 0x01 SETPOINT, 0x02 CONTINUOUS, 0x03 CLOTHES_DRY
  switch (m) {
    case climate::CLIMATE_MODE_AUTO:      return 0x00;  // smart
    case climate::CLIMATE_MODE_DRY:       return 0x01;  // setpoint
    case climate::CLIMATE_MODE_FAN_ONLY:  return 0x02;  // continuous
    case climate::CLIMATE_MODE_HEAT:      return 0x03;  // clothes dry (UI shows "Heat")
    default:                              return 0x00;  // fallback smart
  }
}

climate::ClimateMode MideaDehumComponent::map_mode_for_ui_(uint8_t proto_mode) {
  switch (proto_mode) {
    case 0x00: return climate::CLIMATE_MODE_AUTO;      // smart
    case 0x01: return climate::CLIMATE_MODE_DRY;       // setpoint
    case 0x02: return climate::CLIMATE_MODE_FAN_ONLY;  // continuous
    case 0x03: return climate::CLIMATE_MODE_HEAT;      // clothes dry
    default:   return climate::CLIMATE_MODE_AUTO;
  }
}

// Fan mapping
uint8_t MideaDehumComponent::encode_fan_(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW:    return 0x01;
    case climate::CLIMATE_FAN_MEDIUM: return 0x02;
    case climate::CLIMATE_FAN_HIGH:   return 0x03;
    default:                          return 0x00;  // unsupported
  }
}

climate::ClimateFanMode MideaDehumComponent::map_fan_for_ui_(uint8_t proto_fan) {
  switch (proto_fan) {
    case 0x01: return climate::CLIMATE_FAN_LOW;
    case 0x02: return climate::CLIMATE_FAN_MEDIUM;
    case 0x03: return climate::CLIMATE_FAN_HIGH;
    default:   return climate::CLIMATE_FAN_MEDIUM;
  }
}

}  // namespace midea_dehum
}  // namespace esphome
