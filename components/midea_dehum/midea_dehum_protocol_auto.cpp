// Auto-detect state machine implementation — only compiled when protocol_version: 0
// is configured in YAML. Separated from midea_dehum.cpp to keep the main file
// free of #ifdef MIDEA_PROTOCOL_AUTO guards.

#ifdef MIDEA_PROTOCOL_AUTO

#include "midea_dehum_protocol_auto.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

static const uint32_t AUTO_DETECT_DELAYS[] = {3000, 6000, 12000, 24000, 48000};

void ad_init(MideaDehumComponent* self) {
  auto& ad        = self->ad_state_;
  ad.active       = true;
  ad.round        = 0;
  ad.got_response = false;
  self->set_protocol_ptr(&PROTOCOL_V1);
  ESP_LOGI(TAG, "Protocol auto-detect enabled");
}

void ad_next(MideaDehumComponent* self) {
  auto& ad = self->ad_state_;
  if (!ad.active) return;

  // Check if we got a status response during the last attempt
  if (ad.got_response) {
    ESP_LOGI(TAG, "Auto-detect: protocol v%u received MCU response — locked in",
             self->get_protocol_ptr()->version);
    ad.active = false;
    return;
  }

  ad.round++;

  // Alternate: V1 on odd rounds, V2 on even rounds (V1 first since most common)
  uint8_t try_version = (ad.round % 2 == 1) ? 1 : 2;

  uint8_t delay_idx = ((ad.round - 1) / 2);
  if (delay_idx >= sizeof(AUTO_DETECT_DELAYS) / sizeof(AUTO_DETECT_DELAYS[0]))
    delay_idx = sizeof(AUTO_DETECT_DELAYS) / sizeof(AUTO_DETECT_DELAYS[0]) - 1;
  uint32_t delay_ms = AUTO_DETECT_DELAYS[delay_idx];

  ESP_LOGI(TAG, "Auto-detect round %u: trying v%u (listening %ums)", ad.round, try_version,
           delay_ms);

  // Reset for fresh attempt
  self->set_handshake_step(0);
  self->set_handshake_done(false);
  ad.got_response = false;

  // Select protocol
  self->set_protocol_ptr((try_version == 2) ? &PROTOCOL_V2 : &PROTOCOL_V1);

  // Send init, then check back after the listen window
  self->performHandshakeStep();
  App.scheduler.set_timeout(self, "auto_detect_check", delay_ms,
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
}

bool ad_is_active(const MideaDehumComponent* self) {
  return self->ad_state_.active;
}

bool ad_try_start(MideaDehumComponent* self) {
  if (!self->ad_state_.active) return false;

  self->set_handshake_step(0);
  self->set_handshake_done(false);

  ESP_LOGI(TAG, "Protocol auto-detect: starting detection sequence");
  App.scheduler.set_timeout(self, "auto_detect_start", 100,
                            [self]() { self->protocol_auto_next(); });
  return true;  // caller should skip its own handshake setup
}

}  // namespace midea_dehum
}  // namespace esphome

#endif  // MIDEA_PROTOCOL_AUTO
