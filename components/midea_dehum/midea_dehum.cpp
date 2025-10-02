#include "midea_dehum.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

// ------------------------------------------------------------------
// Simple UART protocol (example):
//   Frame: [0xAA][CMD][DATA][CHK], CHK = sum(all previous bytes) mod 256
// Commands:
//   0x01 POWER   : DATA 0x00=OFF, 0x01=ON
//   0x02 MODE    : DATA 0x00=AUTO, 0x01=DRY(Setpoint), 0x02=CONT, 0x03=FAN_ONLY
//   0x04 TARGET  : DATA 30..80 (target humidity)
//   0x06 ION     : DATA 0x00=OFF, 0x01=ON
//   0x07 TANK    : DATA 0x00=OK,  0x01=FULL
//   0x08 CURRH   : DATA 0..100 (current humidity)
// ------------------------------------------------------------------

static constexpr uint8_t HDR        = 0xAA;
static constexpr uint8_t CMD_POWER  = 0x01;
static constexpr uint8_t CMD_MODE   = 0x02;
static constexpr uint8_t CMD_TARGET = 0x04;
static constexpr uint8_t CMD_ION    = 0x06;
static constexpr uint8_t CMD_TANK   = 0x07;
static constexpr uint8_t CMD_CURRH  = 0x08;

// ----- IonSwitch (child entity) -----------------------------------

void IonSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->set_ion_state(state);
  }
  // Optimistic reflect; will be corrected by feedback frame.
  this->publish_state(state);
}

// ----- Component lifecycle ----------------------------------------

void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier initialized (Climate)");

  // Provide an initial state so HA immediately gets an entity
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_humidity = 50;       // sensible default
  this->current_humidity = NAN;     // unknown until first report
  this->publish_state();

  // (Optional) You could request a status frame from the device here
  // send_command_({HDR, 0x09 /*STATUS_REQ*/, 0x00}); // if your device supports it
}

void MideaDehumComponent::loop() {
  // Gather bytes from UART and parse frames
  while (this->available()) {
    const uint8_t b = this->read();
    rx_buf_.push_back(b);

    // Keep buffer from growing unbounded due to noise
    if (rx_buf_.size() > 64) {
      // Try to resync on header; drop old data if needed
      auto it = std::find(rx_buf_.begin(), rx_buf_.end(), HDR);
      if (it != rx_buf_.begin()) rx_buf_.erase(rx_buf_.begin(), it);
      if (rx_buf_.size() > 64) rx_buf_.clear();
    }

    // Align to header byte
    while (!rx_buf_.empty() && rx_buf_.front() != HDR)
      rx_buf_.erase(rx_buf_.begin());

    // Minimal fixed-size frame in this simplified protocol
    while (rx_buf_.size() >= 4) {
      std::vector<uint8_t> frame(rx_buf_.begin(), rx_buf_.begin() + 4);
      parse_frame_(frame);
      rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 4);
    }
  }
}

// ----- Climate description ----------------------------------------

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;

  // Humidity (current + target)
  t.set_supports_current_humidity(true);
  t.set_supports_target_humidity(true);
  // Visual range for HA UI
  t.set_visual_min_humidity(30);
  t.set_visual_max_humidity(80);

  // Supported modes: keep power in climate (no separate power switch)
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_AUTO);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);
  t.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);

  return t;
}

// ----- Climate control handler ------------------------------------

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  bool changed = false;

  // Handle HVAC mode changes
  if (call.get_mode().has_value()) {
    const auto requested = *call.get_mode();
    switch (requested) {
      case climate::CLIMATE_MODE_OFF:
        // Power off
        send_command_({HDR, CMD_POWER, 0x00});
        this->mode = climate::CLIMATE_MODE_OFF;
        changed = true;
        break;

      case climate::CLIMATE_MODE_AUTO:
        // Ensure power ON then set AUTO
        send_command_({HDR, CMD_POWER, 0x01});
        send_command_({HDR, CMD_MODE,  0x00});
        this->mode = climate::CLIMATE_MODE_AUTO;
        changed = true;
        break;

      case climate::CLIMATE_MODE_DRY:
        // Ensure power ON then set DRY (setpoint)
        send_command_({HDR, CMD_POWER, 0x01});
        send_command_({HDR, CMD_MODE,  0x01});
        this->mode = climate::CLIMATE_MODE_DRY;
        changed = true;
        break;

      case climate::CLIMATE_MODE_FAN_ONLY:
        // Ensure power ON then set FAN_ONLY
        send_command_({HDR, CMD_POWER, 0x01});
        send_command_({HDR, CMD_MODE,  0x03});
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        changed = true;
        break;

      default:
        ESP_LOGW(TAG, "Requested unsupported mode");
        break;
    }
  }

  // Handle target humidity
  if (call.get_target_humidity().has_value()) {
    int h = *call.get_target_humidity();
    if (h < 30) h = 30;
    if (h > 80) h = 80;
    send_command_({HDR, CMD_TARGET, static_cast<uint8_t>(h)});
    this->target_humidity = h;
    changed = true;
  }

  if (changed) this->publish_state();
}

// ----- Auxiliary control (ION) ------------------------------------

void MideaDehumComponent::set_ion_state(bool state) {
  ESP_LOGI(TAG, "Setting ION: %s", state ? "ON" : "OFF");
  send_command_({HDR, CMD_ION, state ? 0x01 : 0x00});
  // feedback will arrive via parse_frame_
}

// ----- UART frame parsing -----------------------------------------

static inline climate::ClimateMode _mode_from_byte(uint8_t b) {
  switch (b) {
    case 0x00: return climate::CLIMATE_MODE_AUTO;
    case 0x01: // DRY (setpoint)
    case 0x02: // continuous (map to DRY semantics)
      return climate::CLIMATE_MODE_DRY;
    case 0x03: return climate::CLIMATE_MODE_FAN_ONLY;
    default:   return climate::CLIMATE_MODE_AUTO;
  }
}

void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 4) return;
  if (frame[0] != HDR) return;

  const uint8_t calc = checksum_(std::vector<uint8_t>(frame.begin(), frame.end() - 1));
  if (calc != frame.back()) {
    ESP_LOGW(TAG, "Checksum mismatch: got 0x%02X expected 0x%02X", frame.back(), calc);
    return;
  }

  const uint8_t cmd  = frame[1];
  const uint8_t data = frame[2];
  bool publish = false;

  switch (cmd) {
    case CMD_POWER:
      // Map power feedback to OFF/AUTO (AUTO denotes "on" default)
      this->mode = (data == 0x00) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_AUTO;
      publish = true;
      break;

    case CMD_MODE:
      this->mode = _mode_from_byte(data);
      publish = true;
      break;

    case CMD_TARGET:
      // Treat as target report; many devices also echo current humidity elsewhere
      this->target_humidity = data;
      publish = true;
      break;

    case CMD_CURRH:
      this->current_humidity = data;
      publish = true;
      break;

    case CMD_ION:
      if (this->ion_switch_ != nullptr) this->ion_switch_->publish_state(data != 0);
      break;

    case CMD_TANK:
      if (this->tank_full_sensor_ != nullptr) this->tank_full_sensor_->publish_state(data != 0);
      break;

    default:
      ESP_LOGD(TAG, "Unhandled cmd=0x%02X data=0x%02X", cmd, data);
      break;
  }

  if (publish) this->publish_state();
}

// ----- UART helpers -----------------------------------------------

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd_wo_chk) {
  std::vector<uint8_t> out = cmd_wo_chk;
  // Ensure header present
  if (out.empty() || out[0] != HDR) {
    out.insert(out.begin(), HDR);
  }
  // Append checksum
  out.push_back(checksum_(out));
  // Send
  for (auto b : out) this->write_byte(b);
  ESP_LOGV(TAG, "TX: %02X %02X %02X %02X", out.size() > 0 ? out[0] : 0, out.size() > 1 ? out[1] : 0,
           out.size() > 2 ? out[2] : 0, out.size() > 3 ? out[3] : 0);
}

uint8_t MideaDehumComponent::checksum_(const std::vector<uint8_t> &bytes) {
  uint8_t sum = 0;
  for (auto b : bytes) sum += b;
  return sum;
}

}  // namespace midea_dehum
}  // namespace esphome
