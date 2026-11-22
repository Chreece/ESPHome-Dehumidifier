// Auto-detect state machine implementation — only compiled when protocol_version: 0
// is configured in YAML. Separated from midea_dehum.cpp to keep the main file
// free of #ifdef MIDEA_PROTOCOL_AUTO guards.

#ifdef MIDEA_PROTOCOL_AUTO

#include "midea_dehum_protocol_auto.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

// Keep the first two probes in their original order. An ACK selects the
// handshake to continue; a valid status subsequently confirms that choice.
static const uint32_t AUTO_DETECT_INTERVAL_MS = 1000;
static const uint32_t AUTO_DETECT_TIMEOUT_MS  = 120000;  // give up after 2 minutes
static const uint32_t LOCK_WATCHDOG_MS        = 30000;   // revert if no status after lock

void ad_init(MideaDehumComponent* self) {
  auto& ad        = self->ad_state_;
  ad.active       = true;
  ad.failed       = false;
  ad.round        = 0;
  ad.got_response = false;
  ad.start_ms     = 0;
  ad.locked_ms    = 0;
  ad.lock_pending = false;
  self->set_protocol_ptr(&PROTOCOL_V1);
  ESP_LOGI(TAG, "Protocol auto-detect enabled");
}

void ad_next(MideaDehumComponent* self) {
  auto& ad = self->ad_state_;
  if (!ad.active) return;  // already locked (via ad_on_ack) — stop cycling

  // Give up (instead of cycling forever) once the overall window elapses.
  if (millis() - ad.start_ms >= AUTO_DETECT_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Auto-detect: no MCU response after %us — giving up",
             (unsigned) (AUTO_DETECT_TIMEOUT_MS / 1000));
    ad.active = false;
    ad.failed = true;
    // Keep legacy fallback polling if the last unanswered probe was V3.
    if (self->get_protocol_ptr() == &PROTOCOL_V3) self->set_protocol_ptr(&PROTOCOL_V2);
    ad.lock_pending = false;
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
    // Allow recovery polls without claiming that a status confirmed discovery.
    self->reset_handshake_polling();
    self->allow_fallback_polling();
#endif
#ifdef USE_MIDEA_DEHUM_PROTOCOL
    self->publish_protocol_text();
#endif
    return;
  }

  ad.round++;

  static const ProtocolVTable* const candidates[] = {&PROTOCOL_V1, &PROTOCOL_V2, &PROTOCOL_V3};
  const auto* candidate = candidates[(ad.round - 1) % 3];
  ESP_LOGI(TAG, "Auto-detect round %u: sending v%u handshake init", ad.round, candidate->version);

  // Reset for a fresh single-shot attempt
  self->set_handshake_step(0);
  self->reset_handshake_polling();
  ad.got_response = false;
  ad.lock_pending = false;

  // Select protocol
  self->set_protocol_ptr(candidate);
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  self->publish_protocol_text();  // reflect the protocol being tried this round
#endif

  // Send one init, then check back after the listen window
  self->performHandshakeStep();
  App.scheduler.set_timeout(self, "auto_detect_check", AUTO_DETECT_INTERVAL_MS,
                            [self]() { self->protocol_auto_next(); });
}

bool ad_prepare_status(MideaDehumComponent* self, uint8_t* data, size_t len) {
  if (self->user_protocol_version_ != 0 || (self->ad_state_.got_response && !self->is_auto_detect())) return true;
  if (v3_status_header(data, len)) {
    // Discard a damaged short frame before a legacy C8 decoder can accept it.
    if (!PROTOCOL_V3.is_status_response(data, len)) return false;
    if (self->get_protocol_ptr() != &PROTOCOL_V3 || self->is_auto_detect() || ad_failed(self))
      self->switch_protocol(&PROTOCOL_V3, false);
  } else if (self->get_protocol_ptr() == &PROTOCOL_V3 && len == 36 &&
             data[0] == 0xAA && data[1] == 0x23 && data[2] == 0xA1) {
    // A long ACK is tentative. A subsequent full-size legacy status takes
    // precedence, including during the V3 probe's one-second listen window.
    const ProtocolVTable* legacy = nullptr;
    if (data[8] == 0x08 && PROTOCOL_V2.is_status_response(data, len))
      legacy = &PROTOCOL_V2;
    else if (PROTOCOL_V1.is_status_response(data, len))
      legacy = &PROTOCOL_V1;
    if (legacy) {
      self->switch_protocol(legacy, false);
      self->set_appliance_type(data[2]);
      self->set_mcu_protocol_version(data[8]);
      self->set_device_info_known(true);
    }
  }
  return true;
}

void ad_on_packet(MideaDehumComponent* self, bool is_status) {
  auto& ad = self->ad_state_;
  if (is_status) {
    // ACK locking clears active, but its watchdog still needs confirmation.
    ad.got_response = true;
    ad.lock_pending = false;
  }
}

void ad_reset(MideaDehumComponent* self) {
  for (const char* name : {"auto_detect_start", "auto_detect_check", "auto_detect_restart",
                           "auto_detect_lock_watchdog"}) {
    App.scheduler.cancel_timeout(self, name);
  }
  self->ad_state_.active = false;
  self->ad_state_.failed = false;
  self->ad_state_.locked_ms = 0;
  self->ad_state_.lock_pending = false;
  self->ad_state_.got_response = false;
}

void ad_on_ack(MideaDehumComponent* self, uint8_t* data, size_t len) {
  auto& ad = self->ad_state_;
  if (!ad.active) return;

  // Use the legacy ACK mapping unless the complete long V3 ACK matches.
  uint8_t version_byte = data[8];
  const ProtocolVTable* proto = (version_byte == 0x08) ? &PROTOCOL_V2 : &PROTOCOL_V1;
  if (v3_ack_header(data, len)) {
    if (!valid_frame_checksum(data, len)) return;
    proto = &PROTOCOL_V3;
  }
  ESP_LOGI(TAG, "Auto-detect: MCU ACK reports protocol v%u (byte8=0x%02X) — locking in",
           proto->version, version_byte);

  // switch_protocol() clears auto-detect state (ad_reset) and re-runs the
  // handshake on the chosen protocol with its normal (bursting) init sequence.
  self->switch_protocol(proto, proto != &PROTOCOL_V3);
  if (proto == &PROTOCOL_V3) proto->on_message(self, data, len);

  // Track this lock attempt separately from permission to poll.
  self->ad_state_.locked_ms = millis();
  self->ad_state_.lock_pending = true;

  // Watchdog: if the locked protocol doesn't produce a status frame within
  // LOCK_WATCHDOG_MS, restart auto-detect. Polling fallback or a V1 handshake
  // ping must not disable recovery from a spurious ACK selecting the wrong protocol.
  App.scheduler.set_timeout(self, "auto_detect_lock_watchdog", LOCK_WATCHDOG_MS, [self]() {
    if (self->ad_state_.lock_pending && !self->ad_state_.got_response &&
        millis() - self->ad_state_.locked_ms >= LOCK_WATCHDOG_MS) {
      ESP_LOGW(TAG, "Auto-detect: locked protocol produced no status after %us — restarting",
               (unsigned) (LOCK_WATCHDOG_MS / 1000));
      self->ad_state_.locked_ms = 0;
      self->ad_state_.lock_pending = false;
      self->ad_state_.active    = true;
      self->ad_state_.round     = 0;
      self->ad_state_.start_ms  = millis();
      // Close the poll gate immediately, including the 100ms restart gap.
      self->set_handshake_step(0);
      self->reset_handshake_polling();
      self->set_protocol_ptr(&PROTOCOL_V1);  // restart from V1
#ifdef USE_MIDEA_DEHUM_PROTOCOL
      self->publish_protocol_text();
#endif
      App.scheduler.set_timeout(self, "auto_detect_restart", 100,
                                [self]() { self->protocol_auto_next(); });
    }
  });
}

bool ad_is_active(const MideaDehumComponent* self) {
  return self->ad_state_.active;
}

bool ad_failed(const MideaDehumComponent* self) {
  return self->ad_state_.failed;
}

bool ad_try_start(MideaDehumComponent* self) {
  if (!self->ad_state_.active) return false;

  self->set_handshake_step(0);
  self->set_handshake_done(false);
  self->ad_state_.start_ms = millis();

  ESP_LOGI(TAG, "Protocol auto-detect: starting detection sequence");
  App.scheduler.set_timeout(self, "auto_detect_start", 100,
                            [self]() { self->protocol_auto_next(); });
  return true;  // caller should skip its own handshake setup
}

}  // namespace midea_dehum
}  // namespace esphome

#endif  // MIDEA_PROTOCOL_AUTO
