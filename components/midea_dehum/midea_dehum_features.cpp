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

#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
void MideaDehumComponent::set_filter_cleaned_button(MideaFilterCleanedButton* b) {
  this->filter_cleaned_button_ = b;
  if (auto* btn = dynamic_cast<MideaFilterCleanedButton*>(b)) {
    btn->set_parent(this);
  }
}
#endif

// ── Filter cleaning button ──────────────────────────────────────────────
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
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

  const size_t offset = 11;  // 10-byte header + 1 byte unknown

  for (size_t i = offset; i + 3 < length && data[i] != 0x00;) {
    uint8_t cap_id   = data[i];
    uint8_t cap_type = data[i + 1];
    uint8_t cap_len  = data[i + 2];
    uint8_t cap_val  = data[i + 3];

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

    // Decode capability value by type
    uint8_t val = cap_val;

    switch (cap_type) {
      case 0x00:  // Boolean presence
        desc += " — Present";
        break;

      case 0x01:  // Numeric range (min/max in next bytes)
        if (cap_len >= 4) {
          float min = data[i + 4] / 2.0f;
          float max = data[i + 5] / 2.0f;
          char buf[64];
          snprintf(buf, sizeof(buf), " — Range %.1f–%.1f", min, max);
          desc += buf;
        } else {
          desc += " — Range unknown";
        }
        break;

      case 0x02:  // Supported values
      {
        desc += " — Supports: ";
        size_t count = val;
        for (size_t j = 0; j < count && i + 4 + j < length; j++) {
          if (j > 0) desc += ", ";
          char buf[8];
          snprintf(buf, sizeof(buf), "%u", data[i + 4 + j]);
          desc += buf;
        }
      } break;

      case 0x03:  // Temperature ranges
        if (cap_len >= 8) {
          float min_cool = data[i + 4] / 2.0f;
          float max_cool = data[i + 5] / 2.0f;
          float min_auto = data[i + 6] / 2.0f;
          float max_auto = data[i + 7] / 2.0f;
          float min_heat = data[i + 8] / 2.0f;
          float max_heat = data[i + 8] / 2.0f;
          char buf[64];
          snprintf(buf, sizeof(buf), " → Cool %.1f–%.1f°C, Auto %.1f–%.1f°C, Heat %.1f–%.1f°C",
                   min_cool, max_cool, min_auto, max_auto, min_heat, max_heat);
          desc += buf;
        } else {
          desc += " → Invalid range";
        }
        break;

      default: {
        char buf[16];
        snprintf(buf, sizeof(buf), " (val=%u)", val);
        desc += buf;
      } break;
    }

    caps.push_back(desc);
    i += 3 + cap_len;
  }

  if (caps.empty()) caps.push_back("No capabilities detected");

  this->update_capabilities_text(caps);
}

void MideaDehumComponent::update_capabilities_text(const std::vector<std::string>& options) {
  if (!this->capabilities_text_) return;

  std::string current = this->capabilities_text_->state;
  std::vector<std::string> existing;

  size_t start = 0;
  while (true) {
    size_t comma     = current.find(',', start);
    std::string item = current.substr(start, comma - start);

    size_t first = item.find_first_not_of(" \t");
    size_t last  = item.find_last_not_of(" \t");
    if (first != std::string::npos && last != std::string::npos)
      item = item.substr(first, last - first + 1);

    if (!item.empty()) existing.push_back(item);

    if (comma == std::string::npos) break;
    start = comma + 1;
  }

  for (const auto& opt : options) {
    bool found = false;
    for (const auto& ex : existing) {
      if (ex == opt) {
        found = true;
        break;
      }
    }
    if (!found) existing.push_back(opt);
  }

  std::string joined;
  for (size_t i = 0; i < existing.size(); i++) {
    joined += existing[i];
    if (i + 1 < existing.size()) joined += ", ";
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
      if (fabs(current - hours) > 0.01f) this->timer_number_->publish_state(hours);
    }

    this->sendSetStatus();
  } else {
    if (this->timer_number_) {
      float current = this->timer_number_->state;
      if (fabs(current - hours) > 0.01f) this->timer_number_->publish_state(hours);
    }
  }
}

void MideaTimerNumber::control(float value) {
  if (!this->parent_) return;
  this->parent_->set_timer_hours(value, false);
}
#endif

}  // namespace midea_dehum
}  // namespace esphome
