// Protocol V2 - MAD50PS1QWT-A verified implementation.
#ifdef MIDEA_PROTOCOL_V2

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "midea_dehum.h"

namespace esphome {
namespace midea_dehum {

static const char* const TAG = "midea_dehum";

// ── V2 init frames (43 bytes: 1a + 1b) ──────────────────────────────────

static uint8_t initPair_v2[43] = {
    0xAA, 0x0B, 0xFF, 0xF4, 0x00, 0x00, 0x01, 0x00, 0x08, 0x07, 0x00, 0xF2,  // 1a
    0xAA, 0x1E, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x08, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94  // 1b
};

// ── V2 handshake responses ───────────────────────────────────────────────

static uint8_t dongleInfoResponse_v2[12] = {0xAA, 0x0B, 0xA1, 0xAA, 0x00, 0x00,
                                            0x00, 0x00, 0x08, 0xE1, 0x00, 0xC1};

static uint8_t netStatusAcquiring_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDA};

static uint8_t netStatusWifi_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C};

static uint8_t netStatusWifiIp_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x04, 0x6E, 0x0A, 0xA8,
    0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00, 0x03, 0x00, 0x00, 0x00, 0x7B};

// ── V2 status query ──────────────────────────────────────────────────────

static uint8_t statusQuery_v2[] = {0xAA, 0x0E, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x03, 0x03, 0xB5, 0x01, 0x11, 0x8E, 0xF6};

// ── V2 helpers ───────────────────────────────────────────────────────────

static void v2_start_handshake(MideaDehumComponent* self) {
  // Stop cycling once handshake is done (status response received)
  if (self->get_handshake_done()) return;

  static uint8_t burst_count = 0;
  burst_count++;
  ESP_LOGI(TAG, "V2 init burst #%d (43 bytes, 1a+1b)", burst_count);
  self->write_array(initPair_v2, sizeof(initPair_v2));

  // Cycle every ~800ms until MCU responds
  App.scheduler.set_timeout(self, "v2_init_repeat", 800,
                            [self]() { self->performHandshakeStep(); });
}

static bool v2_is_status_response(uint8_t* data, size_t len) {
  // MAD50PS1QWT-A: data[9]==0x05 && data[10]==0xA0
  return (len > 10 && data[9] == 0x05 && data[10] == 0xA0);
}

static bool v2_on_message(MideaDehumComponent* self, uint8_t* data, size_t len) {
  if (len < 10) return false;

  // Device ACK during broadcast (step 0) → send dongleInfo + acquiring network status
  if (data[9] == 0x07 && self->get_handshake_step() == 0) {
    self->set_appliance_type(data[2]);
    self->set_mcu_protocol_version(data[7]);
    self->set_device_info_known(true);
    ESP_LOGI(TAG, "V2 ACK received, sending 3a+3b(acquiring)");
    self->write_array(dongleInfoResponse_v2, sizeof(dongleInfoResponse_v2));
    self->write_array(netStatusAcquiring_v2, sizeof(netStatusAcquiring_v2));
    self->set_handshake_step(1);
    return true;
  }

  // E1 info query → send WiFi network status (no IP yet)
  if (data[9] == 0xE1) {
    ESP_LOGI(TAG, "V2 E1 query, sending NetStatus WiFi");
    self->write_array(netStatusWifi_v2, sizeof(netStatusWifi_v2));
    return true;
  }

  // 0xA0 response → send 2× WiFi+IP (MCU sends seed status after 2nd)
  if (data[9] == 0xA0) {
    ESP_LOGI(TAG, "V2 0xA0, sending 2x NetStatus WiFi+IP");
    self->write_array(netStatusWifiIp_v2, sizeof(netStatusWifiIp_v2));
    self->write_array(netStatusWifiIp_v2, sizeof(netStatusWifiIp_v2));
    return true;
  }

  // Network status request
  if (len > 10 && data[10] == 0x63) {
    self->write_array(netStatusWifiIp_v2, sizeof(netStatusWifiIp_v2));
    return true;
  }

  return false;
}

static size_t v2_get_status_query(uint8_t* buf, size_t max_len) {
  if (max_len >= sizeof(statusQuery_v2)) {
    memcpy(buf, statusQuery_v2, sizeof(statusQuery_v2));
    return sizeof(statusQuery_v2);
  }
  return 0;
}

// ── V2 control command ───────────────────────────────────────────────────

static void v2_send_set_status(MideaDehumComponent* self) {
  uint8_t cmd[25];
  memset(cmd, 0, sizeof(cmd));

  const auto& s = self->get_state();

  // [0] Write command marker
  cmd[0] = 0x48;

  // [1] Power: 0x43=ON, 0x42=OFF
  cmd[1] = s.powerOn ? 0x43 : 0x42;

  // [2] Mode
  uint8_t mode = s.mode;
  if (mode < 1 || mode > 4) mode = 3;
  cmd[2] = mode & 0x0F;

  // [3] Fan: 0xA8=Low, 0xD0=High
  cmd[3] = (s.fanSpeed > 50) ? 0xD0 : 0xA8;

  // [4-6] Fixed (no timer fields in V2 command format)
  cmd[4] = 0x7F;
  cmd[5] = 0x7F;
  cmd[6] = 0x00;

  // [7] Target humidity
  cmd[7] = s.humiditySetpoint;

  // [8] Fixed
  cmd[8] = 0x00;

  // [9] Pump: 0x18=ON, 0x10=OFF
#ifdef USE_MIDEA_DEHUM_PUMP
  cmd[9] = self->pump_state_ ? 0x18 : 0x10;
#else
  cmd[9] = 0x10;
#endif

  // [10-14] Padding
  // [15] Water level threshold (not controlled by ESPHome)
  // [16] Fixed
  cmd[16] = 0x01;
  // [17-24] Padding (already zero from memset)

  self->sendMessage(0x02, 0x03, 0x00, 25, cmd);
}

// ── Public vtable instance ────────────────────────────────────────────────

const ProtocolVTable PROTOCOL_V2 = {
    .version            = 2,
    .start_handshake    = v2_start_handshake,
    .is_status_response = v2_is_status_response,
    .on_message         = v2_on_message,
    .get_status_query   = v2_get_status_query,
    .send_set_status    = v2_send_set_status,
    .startup_delay_ms   = 100,
};

}  // namespace midea_dehum
}  // namespace esphome

#endif  // MIDEA_PROTOCOL_V2
