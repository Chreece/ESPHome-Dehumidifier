// Feature implementations extracted from midea_dehum.cpp.
//
// Contains all optional feature method bodies (ion, pump, beep, sleep, filter,
// timer, capabilities) that were previously intermixed with core logic.
// Separated to improve modularity — these only call sendSetStatus() and have
// no coupling to protocol specifics.
//
// Included from midea_dehum.h, compiled unconditionally; feature bodies are
// guarded by their own #ifdef USE_MIDEA_DEHUM_* blocks.

#include <cmath>

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

// ── Sensor wire-up stubs ──────────────────────────────────────────────

#ifdef USE_MIDEA_DEHUM_ERROR
void MideaDehumComponent::set_error_sensor(sensor::Sensor* s) {
  this->error_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
void MideaDehumComponent::set_tank_level_sensor(sensor::Sensor* s) {
  this->tank_level_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_PM25
void MideaDehumComponent::set_pm25_sensor(sensor::Sensor* s) {
  this->pm25_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_BUCKET
void MideaDehumComponent::set_bucket_full_sensor(binary_sensor::BinarySensor* s) {
  this->bucket_full_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_DEFROST
void MideaDehumComponent::set_defrost_sensor(binary_sensor::BinarySensor* s) {
  this->defrost_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_FILTER
void MideaDehumComponent::set_filter_request_sensor(binary_sensor::BinarySensor* s) {
  this->filter_request_sensor_ = s;
}
#endif

// ── Filter cleaning button ──────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
void MideaDehumComponent::set_filter_cleaned_button(MideaFilterCleanedButton* b) {
  this->filter_cleaned_button_ = b;
  if (auto* btn = dynamic_cast<MideaFilterCleanedButton*>(b)) {
    btn->set_parent(this);
  }
}

void MideaFilterCleanedButton::press_action() {
#ifdef USE_MIDEA_DEHUM_FILTER
  if (this->parent_ == nullptr) return;

  if (this->parent_->is_filter_request_active()) {
    this->parent_->set_filter_cleaned_flag(true);
    this->parent_->sendSetStatus();
  }
#endif
}
#endif

// ── Reset water level button ────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
void MideaDehumComponent::set_reset_water_level_button(MideaResetWaterLevelButton* b) {
  this->reset_water_level_button_ = b;
  if (auto* btn = dynamic_cast<MideaResetWaterLevelButton*>(b)) {
    btn->set_parent(this);
  }
}

void MideaResetWaterLevelButton::press_action() {
  if (this->parent_ == nullptr) return;
  this->parent_->sendResetWaterLevel();
}
#endif

// ── IONizer ──────────────────────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_ION
void MideaDehumComponent::set_ion_state(bool on) {
  if (this->ion_state_ == on) return;
  this->ion_state_ = on;
  if (this->ion_switch_) this->ion_switch_->publish_state(on);
  this->sendSetStatus();
}

void MideaDehumComponent::set_ion_switch(MideaIonSwitch* s) {
  this->ion_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaIonSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_ion_state(state);
}
#endif

// ── Drain pump ───────────────────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_PUMP
void MideaDehumComponent::set_pump_state(bool on) {
  if (this->pump_state_ == on) return;
  this->pump_state_ = on;
  if (this->pump_switch_ != nullptr) this->pump_switch_->publish_state(on);
  this->sendSetStatus();
}

void MideaDehumComponent::set_pump_switch(MideaPumpSwitch* s) {
  this->pump_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaPumpSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_pump_state(state);
}
#endif

// ── Buzzer / beep control ────────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_BEEP
void MideaDehumComponent::set_beep_state(bool on) {
  bool was = this->beep_state_;
  if (was == on) {
    return;
  }

  this->beep_state_ = on;

  auto pref = global_preferences->make_preference<bool>(0xBEE1234);
  pref.save(&this->beep_state_);

  if (this->beep_switch_) {
    this->beep_switch_->publish_state(this->beep_state_);
  }

  this->sendSetStatus();
}

void MideaDehumComponent::restore_beep_state() {
  auto pref        = global_preferences->make_preference<bool>(0xBEE1234);
  bool saved_state = false;
  if (pref.load(&saved_state)) {
    this->beep_state_ = saved_state;
  } else {
    this->beep_state_ = false;
  }

  if (this->beep_switch_) {
    this->beep_switch_->publish_state(this->beep_state_);
  }
}

void MideaDehumComponent::set_beep_switch(MideaBeepSwitch* s) {
  this->beep_switch_ = s;
  if (s) {
    s->set_parent(this);
    s->publish_state(this->beep_state_);
  }
}

void MideaBeepSwitch::write_state(bool state) {
  if (!this->parent_) return;

  this->parent_->set_beep_state(state);
}
#endif

// ── Sleep mode ───────────────────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_SLEEP
void MideaDehumComponent::set_sleep_state(bool on) {
  if (this->sleep_state_ == on) return;
  this->sleep_state_ = on;
  if (this->sleep_switch_) this->sleep_switch_->publish_state(on);
  this->sendSetStatus();
}

void MideaDehumComponent::set_sleep_switch(MideaSleepSwitch* s) {
  this->sleep_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaSleepSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_sleep_state(state);
}
#endif

// ── Device capabilities (BETA) ───────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_CAPABILITIES

struct CapabilityMap {
  uint8_t id;
  uint8_t type;
  const char* name;
};

static const CapabilityMap CAPABILITY_TABLE[] = {
    {0x10, 0x02, "Fan speed control"},
    {0x12, 0x02, "ECO mode"},
    {0x13, 0x02, "8°C heating / freeze protection"},
    {0x14, 0x02, "Mode selection"},
    {0x15, 0x02, "Swing control"},
    {0x16, 0x02, "Power consumption / calibration"},
    {0x17, 0x02, "Filter reminder"},
    {0x18, 0x02, "No-wind comfort / silky cool"},
    {0x19, 0x02, "PTC heater / aux heating"},
    {0x1A, 0x02, "Turbo fan / strong mode"},
    {0x1E, 0x02, "Ionizer"},
    {0x1F, 0x02, "Auto humidity control"},
    {0x21, 0x02, "Filter check"},
    {0x22, 0x02, "Temperature Unit Changeable"},
    {0x24, 0x02, "Display / light control"},
    {0x25, 0x02, "Temperature range"},
    {0x2A, 0x02, "Strong fan (alt)"},
    {0x2C, 0x02, "Buzzer / beep control"},
    {0x1D, 0x02, "Drain pump control"},
    {0x20, 0x02, "Clothes Drying"},
    {0x2D, 0x02, "Water level sensor"},
    {0x09, 0x00, "Vertical swing support"},
    {0x0A, 0x00, "Horizontal swing support"},
    {0x15, 0x00, "Indoor humidity sensor"},
    {0x18, 0x00, "No wind feel mode"},
    {0x30, 0x00, "Smart eye / energy save on absence"},
    {0x32, 0x00, "Blowing people"},
    {0x33, 0x00, "Avoid people"},
    {0x39, 0x00, "Self clean"},
    {0x42, 0x00, "Prevent direct fan / one-key no wind"},
    {0x43, 0x00, "Breeze control"}};

void MideaDehumComponent::processCapabilitiesPacket(uint8_t* data, size_t length) {
  if (length < 14) return;
  std::vector<std::string> caps;

  // Frame: [0..9] header, [10]=0xB5 caps discriminator, [11]=capability count,
  // [12..] capability groups. Each group is: id, type, len, <len value bytes>.
  const uint8_t cap_count = data[11];
  const size_t  offset    = 12;
  size_t        seen      = 0;

  for (size_t i = offset; i + 3 < length && data[i] != 0x00 && seen < cap_count;) {
    uint8_t cap_id   = data[i];
    uint8_t cap_type = data[i + 1];
    uint8_t cap_len  = data[i + 2];

    if (cap_len == 0 || i + 3 + cap_len > length) break;

    // Build human-readable description
    const char* name = nullptr;
    for (const auto& entry : CAPABILITY_TABLE) {
      if (entry.id == cap_id && entry.type == cap_type) {
        name = entry.name;
        break;
      }
    }

    std::string desc;
    if (name) {
      desc = name;
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "Unknown 0x%02X", cap_id);
      desc = buf;
    }

    // Feature name only — the capabilities sensor reads best as a plain CSV
    // list of supported features (values/ranges aren't user-meaningful here).
    caps.push_back(desc);
    i += 3 + cap_len;
    seen++;
  }

  if (caps.empty()) caps.push_back("No capabilities detected");

  this->update_capabilities_text(caps);
}

void MideaDehumComponent::update_capabilities_text(const std::vector<std::string>& options) {
  if (!this->capabilities_text_) return;

  // Start from the current published text and append any new options that
  // aren't already present. Uses a single string search per option instead
  // of splitting into a vector — avoids multiple heap allocations.
  std::string joined = this->capabilities_text_->state;

  for (const auto& opt : options) {
    // Check if opt already appears as a whole entry (bounded by comma or
    // string start/end) to avoid false substring matches.
    bool found = false;
    size_t search_from = 0;
    while (true) {
      size_t pos = joined.find(opt, search_from);
      if (pos == std::string::npos) break;
      // Verify word boundaries
      bool left_ok  = (pos == 0 || joined[pos - 1] == ' ' || joined[pos - 1] == ',');
      size_t end_pos = pos + opt.size();
      bool right_ok = (end_pos == joined.size() || joined[end_pos] == ',' || joined[end_pos] == ' ');
      if (left_ok && right_ok) {
        found = true;
        break;
      }
      search_from = pos + 1;
    }

    if (!found) {
      if (!joined.empty()) joined += ", ";
      joined += opt;
    }
  }

  this->capabilities_text_->publish_state(joined);
}

void MideaDehumComponent::getDeviceCapabilities() {
  uint8_t payload[] = {0xB5, 0x01, 0x00, 0x00};

  this->sendMessage(0x03, 0x03, 0x00, sizeof(payload), payload);
}

void MideaDehumComponent::getDeviceCapabilitiesMore() {
  uint8_t payload[] = {0xB5, 0x01, 0x01, 0x00, 0x00};

  this->sendMessage(0x03, 0x03, 0x00, sizeof(payload), payload);
}
#endif

// ── Device timer ─────────────────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_TIMER
void MideaDehumComponent::set_timer_number(MideaTimerNumber* n) {
  this->timer_number_ = n;
  if (n) {
    n->set_parent(this);
    n->publish_state(this->last_timer_hours_);
  }
}

void MideaDehumComponent::set_timer_hours(float hours, bool from_device) {
  hours                   = std::clamp(hours, 0.0f, 24.0f);
  this->last_timer_hours_ = hours;

  if (!from_device) {
    this->timer_write_pending_   = true;
    this->pending_timer_hours_   = hours;
    this->pending_applies_to_on_ = !this->state_.powerOn;

    if (this->timer_number_) {
      float current = this->timer_number_->state;
      if (fabsf(current - hours) > 0.01f) this->timer_number_->publish_state(hours);
    }

    this->sendSetStatus();
  } else {
    if (this->timer_number_) {
      float current = this->timer_number_->state;
      if (fabsf(current - hours) > 0.01f) this->timer_number_->publish_state(hours);
    }
  }
}

void MideaTimerNumber::control(float value) {
  if (!this->parent_) return;
  this->parent_->set_timer_hours(value, false);
}
#endif

#ifdef USE_MIDEA_DEHUM_TARGET_HUMIDITY
void MideaDehumComponent::set_target_humidity_number(MideaTargetHumidityNumber* n) {
  this->target_humidity_number_ = n;
  if (n) {
    n->set_parent(this);
    n->publish_state(this->state_.humiditySetpoint);
  }
}

void MideaTargetHumidityNumber::control(float value) {
  if (!this->parent_) return;
  auto call = this->parent_->make_call();
  call.set_target_humidity(value);
  call.perform();
}
#endif

#ifdef USE_MIDEA_DEHUM_PROTOCOL
void MideaDehumComponent::publish_protocol_text() {
  if (!this->protocol_text_) return;

  std::string s;
  if (this->user_protocol_version_ == 0) {
    if (ad_failed(this)) {
      s = "Auto: no device found";
    } else {
      uint8_t active = this->protocol_ ? this->protocol_->version : 0;
      if (active == 0) {
        s = "Auto (detecting)";
      } else {
        // Version is always a single digit (1 or 2) — avoid std::to_string
        // which pulls in heavy formatting code.
        s = "V";
        s += static_cast<char>('0' + active);
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
        s += this->get_handshake_done() ? " (auto-detected)" : " (auto, trying)";
#else
        s += " (auto)";
#endif
      }
    }
  } else {
    s = "V";
    s += static_cast<char>('0' + this->user_protocol_version_);
    s += " (fixed)";
  }

  if (s != this->last_protocol_str_) {
    this->last_protocol_str_ = s;
    this->protocol_text_->publish_state(s);
  }
}
#endif

}  // namespace midea_dehum
}  // namespace esphome
