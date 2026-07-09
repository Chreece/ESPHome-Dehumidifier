// Status frame decoder — parses the 36-byte status payload into component fields.
// Called from processPacket() when a protocol-specific is_status_response()
// returns true.  Takes a raw byte buffer (serialRxBuf) as input so it has
// zero dependency on the UART transport layer.

#include <cmath>

#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

void MideaDehumComponent::parseState(const uint8_t* buf) {
  bool updated = false;

  // --- Parse core operating parameters ---
  bool new_power           = (buf[11] & 0x01) != 0;
  uint8_t new_mode         = buf[12] & 0x0F;
  uint8_t new_fan          = buf[13] & 0x7F;
  uint8_t new_humidity_set = (buf[17] > 100) ? 99 : buf[17];

  // --- Environmental readings ---
  uint8_t new_humidity = buf[26];
  float temp           = (static_cast<int>(buf[27]) - 50) / 2.0f;
  if (temp < -19.0f) temp = -20.0f;
  if (temp > 50.0f) temp = 50.0f;
  float temperature_decimal = (buf[28] & 0x0F) * 0.1f;
  if (temp >= 0.0f)
    temp += temperature_decimal;
  else
    temp -= temperature_decimal;
  float new_temp    = temp;
#if defined(USE_MIDEA_DEHUM_ERROR) || defined(USE_MIDEA_DEHUM_BUCKET)
  uint8_t new_error = buf[31];
#endif

  // --- Compare and update core state fields ---
  if (new_power != this->state_.powerOn) {
    this->state_.powerOn = new_power;
    updated              = true;
  }
  if (new_mode != this->state_.mode) {
    this->state_.mode = new_mode;
    updated           = true;
  }
  if (new_fan != this->state_.fanSpeed) {
    this->state_.fanSpeed = new_fan;
    updated               = true;
  }
  if (new_humidity_set != this->state_.humiditySetpoint) {
    this->state_.humiditySetpoint = new_humidity_set;
    updated                       = true;
  }
  if (new_humidity != this->state_.currentHumidity) {
    this->state_.currentHumidity = new_humidity;
    updated                      = true;
  }
  if (fabsf(new_temp - this->state_.currentTemperature) > 0.1f) {
    this->state_.currentTemperature = new_temp;
    updated                         = true;
  }

  if (updated || this->first_run_) {
    this->sendClimateState();
  }

#if defined(USE_MIDEA_DEHUM_ERROR) || defined(USE_MIDEA_DEHUM_BUCKET)
  if (this->first_run_ || this->error_state_ != new_error) {
    this->error_state_ = new_error;
#ifdef USE_MIDEA_DEHUM_ERROR
    if (this->error_sensor_) {
      this->error_sensor_->publish_state(this->error_state_);
    }
#endif
  }
#endif

#ifdef USE_MIDEA_DEHUM_BUCKET
  bool bucket_full = (this->error_state_ == 38);
  if (this->first_run_ || bucket_full != this->bucket_full_state_) {
    this->bucket_full_state_ = bucket_full;
    if (this->bucket_full_sensor_) this->bucket_full_sensor_->publish_state(bucket_full);
  }
#endif

#ifdef USE_MIDEA_DEHUM_TIMER
  // --- Parse timer fields from payload bytes 14..16 ---
  const uint8_t on_raw  = buf[14];
  const uint8_t off_raw = buf[15];
  const uint8_t ext_raw = buf[16];

  const bool on_timer_set  = (on_raw & 0x80) != 0;
  const bool off_timer_set = (off_raw & 0x80) != 0;

  uint8_t on_hr = 0, off_hr = 0;
  int on_min = 0, off_min = 0;

  // V2 timer encoding: hours in bits 6-2, quarter-hours (0-3) in bits 1-0.
  // V1 timer encoding: hours in bits 6-2, quarter index in bits 1-0, with a
  // fine-offset subtraction from ext_raw (see protocol_v1 docs).
  const bool is_v2 = (this->protocol_ && this->protocol_->version == 2);

  if (on_timer_set) {
    on_hr  = (on_raw & 0x7C) >> 2;
    if (is_v2) {
      on_min = (on_raw & 0x03) * 15;
    } else {
      on_min = ((on_raw & 0x03) + 1) * 15 - ((ext_raw & 0xF0) >> 4);
      if (on_min < 0) on_min += 60;
    }
  }

  if (off_timer_set) {
    off_hr  = (off_raw & 0x7C) >> 2;
    if (is_v2) {
      off_min = (off_raw & 0x03) * 15;
    } else {
      off_min = ((off_raw & 0x03) + 1) * 15 - (ext_raw & 0x0F);
      if (off_min < 0) off_min += 60;
    }
  }

  const float on_timer_hours  = on_timer_set ? (on_hr + (on_min / 60.0f)) : 0.0f;
  const float off_timer_hours = off_timer_set ? (off_hr + (off_min / 60.0f)) : 0.0f;

  // Cache raw bytes
  this->last_on_raw_  = on_raw;
  this->last_off_raw_ = off_raw;
  this->last_ext_raw_ = ext_raw;

  // Update HA entity with the *active* timer
  float timer_hours = 0.0f;
  if (!this->state_.powerOn && on_timer_set) {
    timer_hours = on_timer_hours;
  } else if (this->state_.powerOn && off_timer_set) {
    timer_hours = off_timer_hours;
  }

  // Always cache the decoded timer hours (needed for tests/diagnostics even
  // when no timer_number_ entity is configured). Only publish to HA if the
  // number entity exists.
  if (this->first_run_ || fabsf(timer_hours - this->last_timer_hours_) > 0.01f) {
    this->last_timer_hours_ = timer_hours;
    if (this->timer_number_) {
      this->set_timer_hours(timer_hours, true);
    }
  }
#endif

  // --- BYTE19 Related features ---

  // --- Panel light / brightness class (bits 7–6) ---
#ifdef USE_MIDEA_DEHUM_LIGHT
  uint8_t new_light_class = (buf[19] & 0xC0) >> 6;
  if (new_light_class != this->light_class_ || this->first_run_) {
    this->light_class_ = new_light_class;
    if (this->light_select_) {
      const char* light_str = new_light_class == 0   ? "Auto"
                              : new_light_class == 1 ? "Off"
                              : new_light_class == 2 ? "Low"
                                                     : "High";
      this->light_select_->publish_state(light_str);
    }
  }
#endif

  // --- Ionizer (bit 6) ---
#ifdef USE_MIDEA_DEHUM_ION
  bool new_ion_state = (buf[19] & 0x40) != 0;
  if (new_ion_state != this->ion_state_ || this->first_run_) {
    if (this->state_.powerOn) {
      this->ion_state_ = new_ion_state;
      if (this->ion_switch_) this->ion_switch_->publish_state(new_ion_state);
    }
  }
#endif

  // --- Sleep mode (bit 5) ---
#ifdef USE_MIDEA_DEHUM_SLEEP
  bool new_sleep_state = (buf[19] & 0x20) != 0;
  if (new_sleep_state != this->sleep_state_ || this->first_run_) {
    if (this->state_.powerOn) {
      this->sleep_state_ = new_sleep_state;
      if (this->sleep_switch_) this->sleep_switch_->publish_state(new_sleep_state);
    }
  }
#endif

  // --- Optional: Pump bits (3–4) ---
#ifdef USE_MIDEA_DEHUM_PUMP
  bool new_pump_state = (buf[19] & 0x08) != 0;
  if (new_pump_state != this->pump_state_ || this->first_run_) {
    if (this->state_.powerOn) {
      this->pump_state_ = new_pump_state;
      if (this->pump_switch_) this->pump_switch_->publish_state(new_pump_state);
    }
  }
#endif

  // --- Filter cleaning bit (7) ---
#ifdef USE_MIDEA_DEHUM_FILTER
  bool new_filter_request = (buf[19] & 0x80) >> 7;
  if (new_filter_request != this->filter_request_state_ || this->first_run_) {
    this->filter_request_state_ = new_filter_request;
    if (this->filter_request_sensor_) {
      this->filter_request_sensor_->publish_state(new_filter_request);
    }
  }
#endif

  // --- Tank / Water Level (Byte 20, bit 0-6) ---
  // Always parsed — V2 protocol needs tank_level_ for cmd[15].
  uint8_t tank_byte      = buf[20];
  uint8_t new_tank_level = tank_byte & 0x7F;

  if (new_tank_level != this->tank_level_ || this->first_run_) {
    this->tank_level_ = new_tank_level;
#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
    if (this->tank_level_sensor_) this->tank_level_sensor_->publish_state(new_tank_level);
#endif
  }

  // --- Defrosting (Byte 20, bit 7) ---
#ifdef USE_MIDEA_DEHUM_DEFROST
  bool new_defrost = (buf[20] & 0x80) != 0;

  if (new_defrost != this->defrost_state_ || this->first_run_) {
    this->defrost_state_ = new_defrost;
    if (this->defrost_sensor_) this->defrost_sensor_->publish_state(new_defrost);
  }
#endif

  // --- PM2.5 value (bytes 23–24) ---
#ifdef USE_MIDEA_DEHUM_PM25
  uint16_t new_pm25_value = static_cast<uint16_t>(buf[23]) | (static_cast<uint16_t>(buf[24]) << 8);
  if (new_pm25_value != this->pm25_ || this->first_run_) {
    this->pm25_ = new_pm25_value;
    if (this->pm25_sensor_) {
      this->pm25_sensor_->publish_state(new_pm25_value);
    }
  }
#endif

  // --- Horizontal swing (byte 29, bit 4) ---
#ifdef USE_MIDEA_DEHUM_HORIZONTAL_SWING
  bool new_horizontal_swing_state = (buf[29] & 0x10) != 0;
  if (new_horizontal_swing_state != this->horizontal_swing_state_ || this->first_run_) {
    if (this->state_.powerOn) {
      this->horizontal_swing_state_ = new_horizontal_swing_state;
      this->sendClimateState();
    }
  }
#endif

  // --- Vertical swing (byte 29, bit 5) ---
#ifdef USE_MIDEA_DEHUM_SWING
  bool new_swing_state = (buf[29] & 0x20) != 0;
  if (new_swing_state != this->swing_state_ || this->first_run_) {
    if (this->state_.powerOn) {
      this->swing_state_ = new_swing_state;
      this->sendClimateState();
    }
  }
#endif

  this->clearRxBuf();
  this->first_run_ = false;
}

}  // namespace midea_dehum
}  // namespace esphome
