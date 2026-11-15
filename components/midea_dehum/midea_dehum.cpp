#include "midea_dehum.h"

#include <cmath>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/version.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

// ── Sensor wire-up stubs moved to midea_dehum_features.cpp ─────────────
// ── parseState() moved to midea_dehum_state.cpp (takes const uint8_t* buf)

void MideaDehumComponent::setup() {
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 4, 0)
  std::vector<const char*> custom_presets;
  if (!this->display_mode_setpoint_.empty() && this->display_mode_setpoint_ != "UNUSED")
    custom_presets.push_back(this->display_mode_setpoint_.c_str());
  if (!this->display_mode_continuous_.empty() && this->display_mode_continuous_ != "UNUSED")
    custom_presets.push_back(this->display_mode_continuous_.c_str());
  if (!this->display_mode_smart_.empty() && this->display_mode_smart_ != "UNUSED")
    custom_presets.push_back(this->display_mode_smart_.c_str());
  if (!this->display_mode_clothes_drying_.empty() && this->display_mode_clothes_drying_ != "UNUSED")
    custom_presets.push_back(this->display_mode_clothes_drying_.c_str());

  if (!custom_presets.empty()) {
    this->set_supported_custom_presets(custom_presets);
  }
#endif

#ifdef USE_MIDEA_DEHUM_BEEP
  this->restore_beep_state();
#endif
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  if (this->handshake_enabled_) {
    this->handshake_step_ = 0;
    this->handshake_done_ = false;

    if (!ad_try_start(this)) {
      uint32_t delay_ms = this->protocol_ ? this->protocol_->startup_delay_ms : 2000;
      ESP_LOGI(TAG, "Protocol v%d, startup delay %ums",
               this->protocol_ ? (int) this->protocol_->version : 0, (unsigned int) delay_ms);
      App.scheduler.set_timeout(this, "start_handshake", delay_ms,
                                [this]() { this->performHandshakeStep(); });
    }
  } else {
    this->handshake_step_ = 2;
    this->handshake_done_ = true;
  }
#else
  this->updateAndSendNetworkStatus(false);
#endif

#ifdef USE_MIDEA_DEHUM_PROTOCOL
  this->publish_protocol_text();
#endif
}

void MideaDehumComponent::loop() {
  this->handleUart();

#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  if (this->handshake_enabled_ && !this->handshake_done_) {
    // Stay silent during the handshake. After the ACK the MCU drives the
    // post-ACK sequence (E1 → A0 → status) on its own; sending status or
    // capability queries mid-handshake disrupts it and the MCU never emits
    // the seed status. Matches the capture-verified dongle behavior.
    //
    // Some MCUs never emit the seed/final status the scripted handshake waits
    // for (observed with units whose ACK reports protocol version 0x03 while
    // the V2 handshake frames carry the capture-verified 0x08). Without a
    // fallback `handshake_done_` stays false forever and the component stays
    // mute: no status queries, no parsed state, stale entities until a user
    // command happens to make the MCU push a status frame.
    // Give the handshake a grace window, then poll anyway — the MCU answers
    // plain 0x41 status queries regardless of how the handshake ended.
    // handshake_fallback_timeout: 0 disables the fallback (wait forever).
    // Auto-detect runs its own (longer) protocol search, so leave it alone.
    if (!this->is_auto_detect() && this->handshake_fallback_ms_ > 0) {
      const uint32_t now = millis();
      if (!this->handshake_wait_timer_running_) {
        this->handshake_wait_timer_running_ = true;
        this->handshake_wait_started_ms_     = now;
      } else if (now - this->handshake_wait_started_ms_ >= this->handshake_fallback_ms_) {
        ESP_LOGW(TAG, "No status frame %ums after handshake start — starting status polling anyway",
                 (unsigned int) this->handshake_fallback_ms_);
        this->handshake_done_ = true;
      }
    }
    if (!this->handshake_done_) {
      return;
    }
  }
#endif

#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  if (!this->capabilities_requested_) {
    this->capabilities_requested_ = true;
    App.scheduler.set_timeout(this, "get_capabilities", 2000,
                              [this]() { this->getDeviceCapabilities(); });
    App.scheduler.set_timeout(this, "get_capabilities_more", 2200,
                              [this]() { this->getDeviceCapabilitiesMore(); });
  }
#endif

  static uint32_t last_status_poll = 0;
  uint32_t now                     = millis();
  if (now - last_status_poll >= this->status_poll_interval_) {
    last_status_poll = now;
    this->getStatus();
  }
}

// ── UART I/O (handleUart, writeHeader, sendMessage, etc.) is in
//    midea_dehum_uart.cpp ─────────────────────────────────────────────────

// Process of the RX Packet received — called from handleUart()
void MideaDehumComponent::processPacket(uint8_t* data, size_t len) {
  // Pretty print packet — guard with ESPHOME_LOG_LEVEL (a compile-time
  // constant defined by esphome/core/log.h) so the std::string heap
  // allocation is completely compiled out when DEBUG logging is off.
  // This is portable across ESP32 and ESP8266, unlike esp_log_level_get().
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  {
    std::string hex_str;
    hex_str.reserve(len * 3);
    for (size_t i = 0; i < len; i++) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", data[i]);
      hex_str += buf;
    }
    ESP_LOGD(TAG, "RX (%zu bytes): %s", len, hex_str.c_str());
  }
#endif

  // Auto-detect: the MCU's reply carries its true protocol version in byte[8]
  // (0x08 = V2, 0x00 = V1). Lock onto that protocol on the first ACK, before the
  // normal vtable dispatch, so detection can't be fooled by which init we sent.
  if (this->is_auto_detect() && len > 9 && data[9] == 0x07) {
    ad_on_ack(this, data[8]);
    this->clearRxBuf();
    return;
  }

  // Status response — discriminated by protocol vtable
  bool is_status_response = (this->protocol_ && this->protocol_->is_status_response(data, len));

  // Auto-detect: a status response confirms the protocol is correct
  ad_on_packet(this, is_status_response);

  if (is_status_response) {
    if (!this->device_info_known_) {
      this->appliance_type_       = data[2];
      this->mcu_protocol_version_ = data[7];
      this->device_info_known_    = true;
    }
    this->parseState(data);
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
    if (!this->handshake_done_) {
      this->handshake_done_ = true;
    }
#endif
    // Overlay only explicitly requested fields onto the real device state.
    this->apply_initial_commands_();
  }
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  // Non-status MCU messages -- dispatched through protocol vtable
  else if (this->protocol_ && this->protocol_->on_message(this, data, len)) {
    // handled by protocol
  }
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  // Capabilities response
  else if (len > 10 && data[10] == 0xB5) {
    this->processCapabilitiesPacket(data, len);
    this->clearRxBuf();
  }
#endif

  // Network Status request — always respond, even without handshake.
  // V1 and V2 use different byte positions for the 0x63 net-status request:
  //   V1: data[10] == 0x63 (sub-type at byte 10, msg type 0x06 at byte 9)
  //   V2: data[9]  == 0x63 (msg type at byte 9)
  // v1_on_message checks data[10], v2_on_message checks data[9]. This fallback
  // covers the V1-style request (data[10]==0x63) when handshake is disabled.
  else if (len > 10 && data[10] == 0x63) {
    this->updateAndSendNetworkStatus(true);
    this->clearRxBuf();
  }
  // Reset WIFI request
  else if (len > 15 && data[0] == 0xAA && data[9] == 0x64 && data[11] == 0x01 && data[15] == 0x01) {
    this->clearRxBuf();
    App.scheduler.set_timeout(this, "factory_reset", 500, [this]() {
      ESP_LOGW(TAG, "Performing factory reset...");
      global_preferences->reset();

      App.scheduler.set_timeout(this, "reboot_after_reset", 300, []() { App.safe_reboot(); });
    });
  }
}

// Apply startup intent once, without restoring any unrequested defaults.
void MideaDehumComponent::apply_initial_commands_() {
  const uint16_t fields = this->initial_fields_;
  auto value = [this](InitialField field) { return (this->initial_bool_values_ & field) != 0; };
  if (fields & INITIAL_POWER) this->state_.powerOn = value(INITIAL_POWER);
  if (fields & INITIAL_MODE) this->state_.mode = this->initial_requested_state_.mode;
  if (fields & INITIAL_FAN) this->state_.fanSpeed = this->initial_requested_state_.fanSpeed;
  if (fields & INITIAL_HUMIDITY)
    this->state_.humiditySetpoint = this->initial_requested_state_.humiditySetpoint;
#ifdef USE_MIDEA_DEHUM_ION
  if (fields & INITIAL_ION) {
    this->ion_state_ = value(INITIAL_ION);
    if (this->ion_switch_) this->ion_switch_->publish_state(this->ion_state_);
  }
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  if (fields & INITIAL_PUMP) {
    this->pump_state_ = value(INITIAL_PUMP);
    if (this->pump_switch_) this->pump_switch_->publish_state(this->pump_state_);
  }
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  if (fields & INITIAL_SLEEP) {
    this->sleep_state_ = value(INITIAL_SLEEP);
    if (this->sleep_switch_) this->sleep_switch_->publish_state(this->sleep_state_);
  }
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  if (fields & INITIAL_SWING) this->swing_state_ = value(INITIAL_SWING);
#endif
#ifdef USE_MIDEA_DEHUM_HORIZONTAL_SWING
  if (fields & INITIAL_HORIZONTAL) this->horizontal_swing_state_ = value(INITIAL_HORIZONTAL);
#endif
  this->initial_fields_ = 0;
#ifdef USE_MIDEA_DEHUM_TIMER
  if (this->pending_set_status_ && this->timer_write_pending_)
    this->pending_applies_to_on_ = !this->state_.powerOn;
#endif
  if (fields) this->sendClimateState();
  if (this->pending_set_status_) {
    this->pending_set_status_ = false;
    this->sendSetStatus();
  }
#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
  if (this->pending_reset_water_level_) {
    this->pending_reset_water_level_ = false;
    this->sendResetWaterLevel();
  }
#endif
}

// ── Handshake delegate — called by scheduler / auto‑detect ──────────────

#ifdef USE_MIDEA_DEHUM_HANDSHAKE
void MideaDehumComponent::performHandshakeStep() {
  // Delegate to protocol vtable
  if (this->protocol_) {
    this->protocol_->start_handshake(this);
  }
}
#endif  // USE_MIDEA_DEHUM_HANDSHAKE

// ═══════════════════════════════════════════════════════════════════════════
//  3. Home Assistant climate control — called from HA via climate::Climate
// ═══════════════════════════════════════════════════════════════════════════

void MideaDehumComponent::control(const climate::ClimateCall& call) {
  bool requestedPowerOn   = this->state_.powerOn;
  uint8_t reqMode            = this->state_.mode;
  uint8_t reqFan             = this->state_.fanSpeed;
  uint8_t reqSet             = this->state_.humiditySetpoint;

  if (call.get_mode().has_value())
    requestedPowerOn = *call.get_mode() != climate::CLIMATE_MODE_OFF;

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 1, 0)
  StringRef requestedPreset = call.get_custom_preset();
#else
  const char* preset = call.get_custom_preset();
  std::string requestedPreset(preset != nullptr ? preset : "");
#endif
  if (!requestedPreset.empty()) {
    if (requestedPreset == display_mode_setpoint_)
      reqMode = 1;
    else if (requestedPreset == display_mode_continuous_)
      reqMode = 2;
    else if (requestedPreset == display_mode_smart_)
      reqMode = 3;
    else if (requestedPreset == display_mode_clothes_drying_)
      reqMode = 4;
    else
      reqMode = 3;  // default fallback
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:
        reqFan = 40;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        // V2 has no MEDIUM — map to HIGH (80). V1 uses 60.
        reqFan = this->is_v2_active() ? 80 : 60;
        break;
      case climate::CLIMATE_FAN_HIGH:
        reqFan = 80;
        break;
      default:
        reqFan = this->is_v2_active() ? 80 : 60;
        break;
    }
  }

  if (call.get_target_humidity().has_value()) {
    float h = *call.get_target_humidity();
    if (h >= 30.0f && h <= 99.0f) reqSet = (uint8_t) lroundf(h);
  }

  if (this->first_run_) {
    if (call.get_mode().has_value())
      this->remember_initial_bool_(INITIAL_POWER, requestedPowerOn);
    if (!requestedPreset.empty()) {
      this->initial_fields_ |= INITIAL_MODE;
      this->initial_requested_state_.mode = reqMode;
    }
    if (call.get_fan_mode().has_value()) {
      this->initial_fields_ |= INITIAL_FAN;
      this->initial_requested_state_.fanSpeed = reqFan;
    }
    if (call.get_target_humidity().has_value() && *call.get_target_humidity() >= 35 &&
        *call.get_target_humidity() <= 85) {
      this->initial_fields_ |= INITIAL_HUMIDITY;
      this->initial_requested_state_.humiditySetpoint = reqSet;
    }
  }

#if defined(USE_MIDEA_DEHUM_SWING) || defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  if (call.get_swing_mode().has_value()) {
#if defined(USE_MIDEA_DEHUM_SWING)
    bool old_vertical = this->swing_state_;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
    bool old_horizontal = this->horizontal_swing_state_;
#endif

    switch (*call.get_swing_mode()) {
      case climate::CLIMATE_SWING_OFF:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = false;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = false;
#endif
        break;

      case climate::CLIMATE_SWING_VERTICAL:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = true;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = false;
#endif
        break;

      case climate::CLIMATE_SWING_HORIZONTAL:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = false;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = true;
#endif
        break;

      case climate::CLIMATE_SWING_BOTH:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = true;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = true;
#endif
        break;

      default:
        break;
    }

    bool changed = false;

#if defined(USE_MIDEA_DEHUM_SWING)
    changed |= (this->swing_state_ != old_vertical);
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
    changed |= (this->horizontal_swing_state_ != old_horizontal);
#endif

#if defined(USE_MIDEA_DEHUM_SWING)
    this->remember_initial_bool_(INITIAL_SWING, this->swing_state_);
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
    this->remember_initial_bool_(INITIAL_HORIZONTAL, this->horizontal_swing_state_);
#endif
    if (changed || this->first_run_) {
      this->sendSetStatus();
      this->sendClimateState();
    }
  }
#endif

  this->handleStateUpdateRequest(requestedPowerOn, reqMode, reqFan, reqSet);
}

void MideaDehumComponent::handleStateUpdateRequest(bool requestedPowerOn, uint8_t mode,
                                                   uint8_t fanSpeed, uint8_t humiditySetpoint) {
  DehumidifierState newState = this->state_;

  newState.powerOn = requestedPowerOn;

  if (mode < 1 || mode > 4) mode = 3;
  newState.mode     = mode;
  newState.fanSpeed = fanSpeed;

  if (humiditySetpoint && humiditySetpoint >= 35 && humiditySetpoint <= 85)
    newState.humiditySetpoint = humiditySetpoint;

  if (newState.powerOn != this->state_.powerOn || newState.mode != this->state_.mode ||
      newState.fanSpeed != this->state_.fanSpeed ||
      newState.humiditySetpoint != this->state_.humiditySetpoint ||
      (this->first_run_ && this->initial_fields_ != 0)) {
    this->state_ = newState;
    this->sendSetStatus();
    this->sendClimateState();
  }
}

void MideaDehumComponent::sendSetStatus() {
  // A set-status frame carries the *entire* device state (power, mode, fan,
  // setpoint, timer, feature flags). Until the first status frame has been
  // parsed, state_ still holds the compile-time defaults (power OFF / Smart /
  // fan 60 → High / 50 %) — sending those would silently reprogram the unit,
  // e.g. a beep-toggle would turn it off and set Smart + High + 50 %.
  // Defer the command instead and flush it once the state is known
  // (see processPacket()).
  if (this->first_run_) {
    if (!this->pending_set_status_) {
      ESP_LOGW(TAG, "Device state unknown (no status frame yet) — deferring command");
    }
    this->pending_set_status_ = true;
    // Only nudge with a status query when polling is allowed at all; during an
    // in-flight handshake an extra query can make the MCU abandon the seed path.
    if (this->handshake_done_) {
      this->getStatus();
    }
    return;
  }

  if (this->protocol_ && this->protocol_->send_set_status) {
    this->protocol_->send_set_status(this);
  }
}

#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
void MideaDehumComponent::sendResetWaterLevel() {
  // Same hazard as sendSetStatus(): the frame mirrors the whole current state,
  // which is still the defaults until the first status frame is parsed.
  if (this->first_run_) {
    if (!this->pending_reset_water_level_) {
      ESP_LOGW(TAG, "Device state unknown (no status frame yet) — deferring reset");
    }
    this->pending_reset_water_level_ = true;
    if (this->handshake_done_) {
      this->getStatus();
    }
    return;
  }

  // "Reset Fill Level" app command — sends a 0x03/0xC8 frame that the MCU
  // interprets as a request to reset the water-level runtime counter.
  // The payload mirrors the current state so the MCU sees a consistent
  // status and zeroes the counter.  Verified against V2 protocol doc.
  //
  // Frame layout (V2 doc): AA 23 A1 ... 08 03 C8 [state payload] [counter] CK
  //   header[8]=0x08 (agreementVersion), header[9]=0x03 (msgType),
  //   payload[0]=0xC8 (reset marker), followed by state bytes.
  uint8_t cmd[25];
  memset(cmd, 0, sizeof(cmd));

  const auto& s = this->state_;
  cmd[0] = 0xC8;  // reset water-level marker

  // Build protocol-specific state payload at cmd[1..]
  if (this->protocol_ && this->protocol_->version == 2) {
    // V2 layout (matches v2_send_set_status byte positions)
    cmd[1] = s.powerOn ? 0x03 : 0x02;
    cmd[2] = (s.mode >= 1 && s.mode <= 4) ? (s.mode & 0x0F) : 3;
    cmd[3] = (s.fanSpeed > 50) ? 0xD0 : 0xA8;
    cmd[7] = s.humiditySetpoint;
    cmd[9] = 0x10;  // pump off (safe default)
    cmd[15] = this->tank_level_;
    cmd[16] = 0x01;
  } else {
    // V1 layout (matches v1_send_set_status byte positions)
    cmd[1] = s.powerOn ? 0x01 : 0x00;
    cmd[2] = (s.mode >= 1 && s.mode <= 4) ? (s.mode & 0x0F) : 3;
    cmd[3] = s.fanSpeed;
    cmd[7] = s.humiditySetpoint;
  }

  this->sendMessage(0x03, 0x08, 0x00, 25, cmd);
}
#endif

void MideaDehumComponent::sendClimateState() {
  // Compute climate mode
  this->mode = this->state_.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;

  // Compute action. The web UI uses the action as the entity "state" (a
  // dehumidifier has no target temperature, so without an action the web UI
  // would show the entity state as a NaN target temperature).
  if (!this->state_.powerOn) {
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (this->state_.currentHumidity > this->state_.humiditySetpoint) {
    this->action = climate::CLIMATE_ACTION_DRYING;
  } else {
    this->action = climate::CLIMATE_ACTION_IDLE;
  }

  // Fan level mapping.
  // V2 only has two speeds: ≤50 → LOW, >50 → HIGH.
  // V1 has three: ≤50 → LOW, ≤70 → MEDIUM, >70 → HIGH.
  if (this->state_.fanSpeed <= 50) {
    this->fan_mode = climate::CLIMATE_FAN_LOW;
  } else if (this->is_v2_active()) {
    this->fan_mode = climate::CLIMATE_FAN_HIGH;
  } else if (this->state_.fanSpeed <= 70) {
    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  } else {
    this->fan_mode = climate::CLIMATE_FAN_HIGH;
  }

  // Determine mode preset
  switch (this->state_.mode) {
    case 1:
      this->set_custom_preset_(display_mode_setpoint_.c_str());
      break;

    case 2:
      this->set_custom_preset_(display_mode_continuous_.c_str());
      break;

    case 3:
      this->set_custom_preset_(display_mode_smart_.c_str());
      break;

    case 4:
      this->set_custom_preset_(display_mode_clothes_drying_.c_str());
      break;

    default:
      this->set_custom_preset_(display_mode_smart_.c_str());
      break;
  }

  this->target_humidity     = int(this->state_.humiditySetpoint);
  this->current_humidity    = int(this->state_.currentHumidity);
  this->current_temperature = this->state_.currentTemperature;
#if defined(USE_MIDEA_DEHUM_SWING) || defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  // Default to OFF
  climate::ClimateSwingMode swing = climate::CLIMATE_SWING_OFF;

#if defined(USE_MIDEA_DEHUM_SWING) && defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  if (this->swing_state_ && this->horizontal_swing_state_) {
    swing = climate::CLIMATE_SWING_BOTH;
  } else if (this->horizontal_swing_state_) {
    swing = climate::CLIMATE_SWING_HORIZONTAL;
  } else if (this->swing_state_) {
    swing = climate::CLIMATE_SWING_VERTICAL;
  }
#elif defined(USE_MIDEA_DEHUM_SWING)
  if (this->swing_state_) {
    swing = climate::CLIMATE_SWING_VERTICAL;
  }
#elif defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  if (this->horizontal_swing_state_) {
    swing = climate::CLIMATE_SWING_HORIZONTAL;
  }
#endif

  this->swing_mode = swing;
#endif

  this->publish_state();  // Update main HA entity

#ifdef USE_MIDEA_DEHUM_TARGET_HUMIDITY
  if (this->target_humidity_number_ != nullptr) {
    float setpoint = this->state_.humiditySetpoint;
    if (fabsf(this->target_humidity_number_->state - setpoint) > 0.01f)
      this->target_humidity_number_->publish_state(setpoint);
  }
#endif
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  this->publish_protocol_text();
#endif
}

// ===== Climate control =======================================================
climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                      climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY |
                      climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY |
                      climate::CLIMATE_SUPPORTS_ACTION);

  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);

  // Fan modes: V2 only supports Low/High. V1 supports Low/Medium/High.
  // For auto-detect (version 0), advertise all three so HA offers the choice
  // before the protocol locks in; V2's encoder maps MEDIUM→High anyway.
  t.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
  if (this->user_protocol_version_ != 2) {
    t.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  }

#if defined(USE_MIDEA_DEHUM_SWING) || defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  climate::ClimateSwingModeMask swing_modes;

  swing_modes.insert(climate::CLIMATE_SWING_OFF);

#if defined(USE_MIDEA_DEHUM_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_VERTICAL);
#endif

#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_HORIZONTAL);
#endif

#if defined(USE_MIDEA_DEHUM_SWING) && defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_BOTH);
#endif
  t.set_supported_swing_modes(swing_modes);
#endif

  t.set_visual_min_humidity(30.0f);
  t.set_visual_max_humidity(80.0f);

#if ESPHOME_VERSION_CODE < VERSION_CODE(2026, 4, 0)
  std::vector<const char*> custom_presets;
  if (!this->display_mode_setpoint_.empty() && this->display_mode_setpoint_ != "UNUSED")
    custom_presets.push_back(this->display_mode_setpoint_.c_str());
  if (!this->display_mode_continuous_.empty() && this->display_mode_continuous_ != "UNUSED")
    custom_presets.push_back(this->display_mode_continuous_.c_str());
  if (!this->display_mode_smart_.empty() && this->display_mode_smart_ != "UNUSED")
    custom_presets.push_back(this->display_mode_smart_.c_str());
  if (!this->display_mode_clothes_drying_.empty() && this->display_mode_clothes_drying_ != "UNUSED")
    custom_presets.push_back(this->display_mode_clothes_drying_.c_str());

  if (!custom_presets.empty()) {
    t.set_supported_custom_presets(custom_presets);
  }
#endif

  return t;
}

void MideaDehumComponent::set_protocol_version(uint8_t version) {
  this->user_protocol_version_ = version;

#ifdef MIDEA_PROTOCOL_AUTO
  if (version == 0) {
    ad_init(this);
    return;
  }
#endif
#ifdef MIDEA_PROTOCOL_V2
  if (version == 2) {
    this->protocol_ = &PROTOCOL_V2;
    return;
  }
#endif
#ifdef MIDEA_PROTOCOL_V1
  this->protocol_ = &PROTOCOL_V1;
#endif
}

void MideaDehumComponent::switch_protocol(const ProtocolVTable* proto) {
  this->protocol_ = proto;
  ad_reset(this);
  this->handshake_step_ = 0;
  this->handshake_done_ = false;
  ESP_LOGI(TAG, "Switched to protocol v%u", proto->version);
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  this->publish_protocol_text();
#endif
  App.scheduler.set_timeout(this, "switch_protocol_init", 100,
                            [this]() { this->performHandshakeStep(); });
}

void MideaDehumComponent::protocol_auto_next() {
  ad_next(this);
}

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent* uart) {
  this->set_uart_parent(uart);
  this->uart_ = uart;
}

}  // namespace midea_dehum
}  // namespace esphome
