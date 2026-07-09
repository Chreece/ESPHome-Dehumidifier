// Protocol V1 - Chreece original implementation.
#ifdef MIDEA_PROTOCOL_V1

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

// V1 dongle announce frame (Chreece original)
static uint8_t dongleAnnounce_v1[12] = {0xAA, 0x0B, 0xFF, 0xF4, 0x00, 0x00,
                                        0x01, 0x00, 0x00, 0x07, 0x00, 0xFA};

// V1 status query frame — pre-built equivalent of:
//   sendMessage(0x03, 0x03, 0x00, 21, getStatusCommand)
static uint8_t statusQuery_v1[] = {0xAA, 0x20, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x03, 0x03, 0x41, 0x81, 0x00, 0xFF, 0x03, 0xFF, 0x00,
                                   0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x03, 0xCD, 0xA4};

// ── V1 helpers ───────────────────────────────────────────────────────────

static void v1_start_handshake(MideaDehumComponent* self) {
  switch (self->get_handshake_step()) {
    case 0:
      // Send dongleAnnounce, advance to step 1
      self->write_array(dongleAnnounce_v1, sizeof(dongleAnnounce_v1));
      self->set_handshake_step(1);
      break;

    case 1: {
      // Send network status message (0xA0)
      uint8_t payload[19];
      memset(payload, 0, sizeof(payload));
      self->sendMessage(0xA0, 0x08, 0xBF, 19, payload);
      self->set_handshake_step(2);
      break;
    }

    case 2:
      // Update and send network status (connected)
      self->updateAndSendNetworkStatus(true);
      break;

    default:
      break;
  }
}

static bool v1_is_status_response(uint8_t* data, size_t len) {
  return (len > 10 && data[10] == 0xC8);
}

static bool v1_on_message(MideaDehumComponent* self, uint8_t* data, size_t len) {
  if (len < 10) return false;

  // Device ACK (0x07) at handshake step 1
  if (data[9] == 0x07 && self->get_handshake_step() == 1) {
    self->set_appliance_type(data[2]);
    self->set_mcu_protocol_version(data[7]);
    self->set_device_info_known(true);
    App.scheduler.set_timeout(self, "handshake_step_1", 200, [self]() {
      self->performHandshakeStep();
      self->clearRxBuf();
    });
    return true;
  }

  // 0xA0 response at handshake step 2
  if (data[9] == 0xA0 && self->get_handshake_step() == 2) {
    App.scheduler.set_timeout(self, "handshake_step_2", 200, [self]() {
      self->performHandshakeStep();
      self->clearRxBuf();
    });
    return true;
  }

  // UART ping — echo back and mark handshake done
  if (data[9] == 0x05 && !self->get_handshake_done()) {
    self->write_array(data, len);
    self->set_handshake_done(true);
    App.scheduler.set_timeout(self, "post_handshake_init", 1500, [self]() { self->getStatus(); });
    return true;
  }

  // Network status request
  if (len > 10 && data[10] == 0x63) {
    self->updateAndSendNetworkStatus(true);
    self->clearRxBuf();
    return true;
  }

  return false;
}

static size_t v1_get_status_query(uint8_t* buf, size_t max_len) {
  if (max_len >= sizeof(statusQuery_v1)) {
    memcpy(buf, statusQuery_v1, sizeof(statusQuery_v1));
    return sizeof(statusQuery_v1);
  }
  return 0;
}

// ── V1 control command ───────────────────────────────────────────────────

static void v1_send_set_status(MideaDehumComponent* self) {
  uint8_t cmd[25];
  memset(cmd, 0, 25);

  const auto& s = self->get_state();

  // [0] Write command marker
  cmd[0] = 0x48;

  // [1] Power and beep
  cmd[1] = s.powerOn ? 0x01 : 0x00;
#ifdef USE_MIDEA_DEHUM_BEEP
  if (self->get_beep_state()) cmd[1] |= 0x40;
#endif

  // [2] Mode
  uint8_t mode = s.mode;
  if (mode < 1 || mode > 4) mode = 3;
  cmd[2] = mode & 0x0F;

  // [3] Fan speed
  cmd[3] = (uint8_t) s.fanSpeed;

#ifdef USE_MIDEA_DEHUM_TIMER
  uint8_t on_raw  = self->get_last_on_raw();
  uint8_t off_raw = self->get_last_off_raw();
  uint8_t ext_raw = self->get_last_ext_raw();

  bool force_timer_apply = false;

  if (self->get_timer_write_pending()) {
    force_timer_apply = true;

    if (self->get_pending_timer_hours() <= 0.01f) {
      on_raw = off_raw = ext_raw = 0x00;
    } else {
      uint16_t total_minutes =
          static_cast<uint16_t>(self->get_pending_timer_hours() * 60.0f + 0.5f);
      uint8_t hours   = total_minutes / 60;
      uint8_t minutes = total_minutes % 60;

      if (minutes == 0 && hours > 0) {
        minutes = 60;
        hours--;
      }

      uint8_t minutesH = minutes / 15;
      uint8_t minutesL = 15 - (minutes % 15);
      if (minutes % 15 == 0) {
        minutesL = 0;
        if (minutesH > 0) minutesH--;
      }

      if (self->get_pending_applies_to_on()) {
        on_raw  = 0x80 | ((hours & 0x1F) << 2) | (minutesH & 0x03);
        ext_raw = (minutesL & 0x0F) << 4;
        off_raw = 0x00;
      } else {
        off_raw = 0x80 | ((hours & 0x1F) << 2) | (minutesH & 0x03);
        ext_raw = (minutesL & 0x0F);
        on_raw  = 0x00;
      }
    }

    self->set_last_on_raw(on_raw);
    self->set_last_off_raw(off_raw);
    self->set_last_ext_raw(ext_raw);
    self->clear_timer_write_pending();
  }

  cmd[4] = on_raw;
  cmd[5] = off_raw;
  cmd[6] = ext_raw;

  if (force_timer_apply || (on_raw & 0x80) || (off_raw & 0x80)) {
    cmd[3] |= 0x80;
#ifdef USE_MIDEA_DEHUM_TIMERMODE_HINT
    cmd[1] |= 0x10;
#endif
  } else {
    cmd[3] &= static_cast<uint8_t>(~0x80);
  }
#endif

  // [7] Target humidity
  cmd[7] = s.humiditySetpoint;

  // [9] Feature flags
  uint8_t b9 = 0;
#ifdef USE_MIDEA_DEHUM_LIGHT
  b9 |= (self->get_light_class() & 0x03) << 6;
#endif
#ifdef USE_MIDEA_DEHUM_ION
  if (self->get_ion_state()) b9 |= 0x40;
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  if (self->get_sleep_state()) b9 |= 0x20;
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  if (self->get_pump_state()) {
    b9 |= 0x18;
  } else {
    b9 |= 0x10;
  }
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  if (self->pop_filter_cleaned_flag()) b9 |= 0x80;
#endif
  cmd[9] = b9;

  // [10] Swing
#ifdef USE_MIDEA_DEHUM_SWING
  uint8_t swing_flags = 0x00;
  if (self->get_swing_state()) swing_flags |= 0x08;
  cmd[10] = swing_flags;
#endif

  self->sendMessage(0x02, 0x03, 0x00, 25, cmd);
}

// ── Public vtable instance ────────────────────────────────────────────────

const ProtocolVTable PROTOCOL_V1 = {
    .version            = 1,
    .start_handshake    = v1_start_handshake,
    .is_status_response = v1_is_status_response,
    .on_message         = v1_on_message,
    .get_status_query   = v1_get_status_query,
    .send_set_status    = v1_send_set_status,
    .startup_delay_ms   = 2000,
};

}  // namespace midea_dehum
}  // namespace esphome

#endif  // MIDEA_PROTOCOL_V1
