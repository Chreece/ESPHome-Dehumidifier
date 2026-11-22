// Protocol V3: short status frames and sequenced polls and controls.
#ifdef MIDEA_PROTOCOL_V3

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

// Captured startup advertisements, in wire order.
static const uint8_t v3_announce[12] = {
    0xAA, 0x0B, 0xFF, 0xF4, 0x00, 0x00, 0x01, 0x00, 0x08, 0x07, 0x00, 0xF2};
static const uint8_t v3_acquiring[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDA};
static const uint8_t v3_wifi[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x67};
static const uint8_t v3_wifi_ip_1[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x04, 0xBA, 0x08, 0xA8,
    0xC0, 0xFF, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x3B};
static const uint8_t v3_wifi_ip_2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x04, 0xBA, 0x08, 0xA8,
    0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x3A};

bool v3_ack_header(const uint8_t* data, size_t len) {
  return len == 43 && data[0] == 0xAA && data[1] == 0x2A && data[2] == 0xA1 &&
         data[8] == 0x03 && data[9] == 0x07;
}

bool v3_status_header(const uint8_t* data, size_t len) {
  return len == 35 && data[0] == 0xAA && data[1] == 0x22 && data[2] == 0xA1 &&
         data[8] == 0x03 && ((data[9] == 0x05 && data[10] == 0xA0) ||
                              ((data[9] == 0x02 || data[9] == 0x03) && data[10] == 0xC8));
}

static bool v3_is_status_response(uint8_t* data, size_t len) {
  return v3_status_header(data, len) && valid_frame_checksum(data, len) && valid_body_crc(data, len);
}

static void v3_start_acquisition(MideaDehumComponent* self) {
  // Captured intervals are relative to the first announce. Each callback
  // belongs to this handshake and is cancelled when discovery changes protocol.
  static constexpr uint16_t delays[] = {50, 383, 712, 1046, 1388, 1708};
  static const char* const names[] = {"v3_acquire_1", "v3_acquire_2", "v3_acquire_3",
                                      "v3_acquire_4", "v3_acquire_5", "v3_acquire_6"};
  for (size_t i = 0; i < 6; ++i)
    App.scheduler.set_timeout(self, names[i], delays[i], [self]() {
      self->write_array(v3_acquiring, sizeof(v3_acquiring));
    });
  App.scheduler.set_timeout(self, "v3_wifi", 2056, [self]() { self->write_array(v3_wifi, sizeof(v3_wifi)); });
}

static void v3_start_handshake(MideaDehumComponent* self) {
  if (self->get_handshake_step() != 0 || self->is_status_polling_allowed()) return;
  self->write_array(v3_announce, sizeof(v3_announce));
  self->set_handshake_step(1);
  // A discovery probe sends only the announce until the peer identifies itself.
  if (!self->is_auto_detect()) v3_start_acquisition(self);
}

static bool v3_on_message(MideaDehumComponent* self, uint8_t* data, size_t len) {
  if (!v3_ack_header(data, len) || !valid_frame_checksum(data, len)) return false;
  if (self->get_handshake_step() >= 2) return true;
  // After an auto probe, the detector passes the actual ACK into this fresh
  // handshake. Fixed V3 already scheduled acquisition alongside the announce.
  if (self->get_handshake_step() == 0) v3_start_acquisition(self);
  self->set_handshake_step(2);
  self->set_appliance_type(data[2]);
  self->set_mcu_protocol_version(data[7]);
  self->set_device_info_known(true);
  App.scheduler.set_timeout(self, "v3_ip_1", 10743, [self]() {
    self->write_array(v3_wifi_ip_1, sizeof(v3_wifi_ip_1));
  });
  App.scheduler.set_timeout(self, "v3_ip_2", 12719, [self]() {
    self->write_array(v3_wifi_ip_2, sizeof(v3_wifi_ip_2));
  });
  return true;
}

static void v3_get_status_query(MideaDehumComponent* self) {
  uint8_t query[21] = {0x41, 0x81, 0x00, 0xFF};
  query[20] = self->next_v3_sequence(false);
  self->sendMessage(0x03, 0, 0, sizeof(query), query);
}

static void v3_send_set_status(MideaDehumComponent* self) {
  const auto& state = self->get_state();
  if (state.mode < 1 || state.mode > 3 || (state.fanSpeed != 40 && state.fanSpeed != 80)) {
    ESP_LOGW(TAG, "Cannot encode V3 mode %u / fan %u", state.mode, state.fanSpeed);
    return;
  }
  uint8_t cmd[22] = {0x48};
  cmd[1] = state.powerOn ? 0x43 : 0x42;
#ifdef USE_MIDEA_DEHUM_BEEP
  if (!self->get_beep_state()) cmd[1] &= ~0x40;
#endif
  cmd[2] = state.mode;
  cmd[3] = state.fanSpeed | 0x80;
#ifdef USE_MIDEA_DEHUM_TIMER
  // Use the V2 quarter-hour layout; V3 timer writes need device verification.
  if (self->get_timer_write_pending()) {
    const float hours = self->get_pending_timer_hours();
    uint8_t on_raw = 0x7F, off_raw = 0x7F, ext_raw = 0x00;
    if (hours > 0.01f) {
      const uint16_t minutes = static_cast<uint16_t>(hours * 60.0f + 0.5f);
      const uint8_t encoded = 0x80 | ((minutes / 60) << 2) | ((minutes % 60) / 15);
      if (self->get_pending_applies_to_on())
        on_raw = encoded;
      else
        off_raw = encoded;
      ext_raw = 0x0F;
    }
    self->set_last_on_raw(on_raw);
    self->set_last_off_raw(off_raw);
    self->set_last_ext_raw(ext_raw);
    self->clear_timer_write_pending();
  }
#endif
  cmd[4] = self->get_last_on_raw();
  cmd[5] = self->get_last_off_raw();
  cmd[6] = self->get_last_ext_raw();
  cmd[7] = state.humiditySetpoint;
#ifdef USE_MIDEA_DEHUM_PUMP
  if (self->v3_pump_requested_) {
    cmd[9] = self->get_pump_state() ? 0x18 : 0x10;
    self->v3_pump_requested_ = false;
  }
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  if (self->pop_filter_cleaned_flag()) cmd[9] |= 0x80;
#endif
  cmd[21] = self->next_v3_sequence(true);
  self->sendMessage(0x02, 0, 0, sizeof(cmd), cmd);
}

const ProtocolVTable PROTOCOL_V3 = {
    .version = 3,
    .start_handshake = v3_start_handshake,
    .is_status_response = v3_is_status_response,
    .on_message = v3_on_message,
    .get_status_query = v3_get_status_query,
    .send_set_status = v3_send_set_status,
    .startup_delay_ms = 0,
};

}  // namespace midea_dehum
}  // namespace esphome

#endif  // MIDEA_PROTOCOL_V3
