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

// Auto-detect cadence: alternate sending the V1 and V2 handshake inits, one per
// AUTO_DETECT_INTERVAL_MS. Detection is not based on which init we sent — the
// MCU's reply carries its true protocol version in frame byte[8] (0x08 = V2,
// 0x00 = V1), which processPacket reads to lock the matching protocol. Give up
// after AUTO_DETECT_TIMEOUT_MS so an absent MCU is not cycled forever.
static const uint32_t AUTO_DETECT_INTERVAL_MS = 1000;   // alternate V1/V2 every 1s
static const uint32_t AUTO_DETECT_TIMEOUT_MS  = 120000;  // give up after 2 minutes
static const uint32_t LOCK_WATCHDOG_MS        = 30000;   // revert if no status after lock

void ad_init(MideaDehumComponent* self) {
  auto& ad        = self->ad_state_;
  ad.active       = true;
  ad.failed       = false;
  ad.round        = 0;
  ad.got_response = false;
  ad.start_ms     = 0;
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
#ifdef USE_MIDEA_DEHUM_PROTOCOL
    self->publish_protocol_text();
#endif
    return;
  }

  ad.round++;

  // Alternate V1/V2 handshake inits, one per cycle (V1 first). The reply's
  // byte[8] — not which init we sent — determines the locked protocol.
  uint8_t try_version = (ad.round % 2 == 1) ? 1 : 2;

  ESP_LOGI(TAG, "Auto-detect round %u: sending v%u handshake init", ad.round, try_version);

  // Reset for a fresh single-shot attempt
  self->set_handshake_step(0);
  self->set_handshake_done(false);
  ad.got_response = false;

  // Select protocol
  self->set_protocol_ptr((try_version == 2) ? &PROTOCOL_V2 : &PROTOCOL_V1);
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  self->publish_protocol_text();  // reflect the protocol being tried this round
#endif

  // Send one init, then check back after the listen window
  self->performHandshakeStep();
  App.scheduler.set_timeout(self, "auto_detect_check", AUTO_DETECT_INTERVAL_MS,
                            [self]() { self->protocol_auto_next(); });
}

void ad_on_packet(MideaDehumComponent* self, bool is_status) {
  auto& ad = self->ad_state_;
  if (ad.active && is_status) {
    ad.got_response = true;
  }
}

void ad_reset(MideaDehumComponent* self) {
  self->ad_state_.active = false;
  self->ad_state_.locked_ms = 0;
}

void ad_on_ack(MideaDehumComponent* self, uint8_t version_byte) {
  auto& ad = self->ad_state_;
  if (!ad.active) return;

  // Frame byte[8] is the MCU's protocol version: 0x08 = V2, otherwise V1.
  const ProtocolVTable* proto = (version_byte == 0x08) ? &PROTOCOL_V2 : &PROTOCOL_V1;
  ESP_LOGI(TAG, "Auto-detect: MCU ACK reports protocol v%u (byte8=0x%02X) — locking in",
           proto->version, version_byte);

  // switch_protocol() clears auto-detect state (ad_reset) and re-runs the
  // handshake on the chosen protocol with its normal (bursting) init sequence.
  self->switch_protocol(proto);

  // Mark as locked *after* switch_protocol (which calls ad_reset and clears
  // locked_ms). The watchdog uses this to know a lock attempt is in progress.
  self->ad_state_.locked_ms = millis();

  // Watchdog: if the locked protocol doesn't produce a status frame within
  // LOCK_WATCHDOG_MS, restart auto-detect. This guards against a spurious ACK
  // locking the wrong protocol. The elapsed-time check ensures we don't fire
  // prematurely if the scheduler drains callbacks faster than real time.
  App.scheduler.set_timeout(self, "auto_detect_lock_watchdog", LOCK_WATCHDOG_MS, [self]() {
    if (!self->get_handshake_done() && self->ad_state_.locked_ms != 0 &&
        millis() - self->ad_state_.locked_ms >= LOCK_WATCHDOG_MS) {
      ESP_LOGW(TAG, "Auto-detect: locked protocol produced no status after %us — restarting",
               (unsigned) (LOCK_WATCHDOG_MS / 1000));
      self->ad_state_.locked_ms = 0;
      self->ad_state_.active    = true;
      self->ad_state_.start_ms  = millis();
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
