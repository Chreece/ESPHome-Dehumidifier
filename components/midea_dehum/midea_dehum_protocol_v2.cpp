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

// NetStatus frames below are copied byte-for-byte from the original RTL8720
// dongle boot capture (dongle-boot-20260630) so the MCU follows its normal
// ACK → E1 → 0xA0 → seed-status path. Mismatched frames make the MCU abandon
// that path and fall back to 0x63 keepalive without ever emitting seed status.

// E1 query response — "WiFi connected, no IP yet" (variant B)
static uint8_t netStatusWifi_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x00, 0x00, 0x5E};

// 0xA0 response — "WiFi + IP" first send (counter 02)
static uint8_t netStatusWifiIp02_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x04, 0x6E, 0x0A, 0xA8,
    0xC0, 0xFF, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x00, 0x00, 0x7C};

// 0xA0 response — "WiFi + IP" second send (counter 03)
static uint8_t netStatusWifiIp_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0D, 0x01, 0x01, 0x04, 0x6E, 0x0A, 0xA8,
    0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00, 0x03, 0x00, 0x00, 0x00, 0x7B};

// 0x63 net-status-request response — same IP payload but msg type 0x63 (not
// 0x0D). The MCU sends 0x63 as a keepalive and expects a 0x63-typed reply.
static uint8_t netStatus63_v2[31] = {
    0xAA, 0x1E, 0xA1, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x08, 0x63, 0x01, 0x01, 0x04, 0x6E, 0x0A, 0xA8,
    0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00, 0x03, 0x00, 0x00, 0x00, 0x25};

// ── V2 status query ──────────────────────────────────────────────────────
// The factory dongle polls status with a 0x41 query (33B), which the MCU
// answers with a 0x03/0xC8 status frame. The older 0x03/0xB5 query only
// returns a capabilities frame (0x03/0xB5), never live status — verified
// against logic-analyzer captures (refresh-function-20260630).

static uint8_t statusQuery_v2[] = {0xAA, 0x20, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x02, 0x03, 0x41, 0x21, 0x00, 0xFF, 0x03, 0x00,
                                   0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x5A, 0x79};

// ── V2 helpers ───────────────────────────────────────────────────────────

static void v2_start_handshake(MideaDehumComponent* self) {
  // Stop cycling once handshake is done (status response received)
  if (self->get_handshake_done()) return;

  uint8_t step = self->get_handshake_step();

  if (step == 0) {
    // Initial handshake — send 1a+1b init pair and keep repeating
    // until MCU ACKs (which sets step=1 via v2_on_message)
    static uint8_t burst_count = 0;
    burst_count++;
    ESP_LOGD(TAG, "V2 init burst #%d (43 bytes, 1a+1b)", burst_count);
    self->write_array(initPair_v2, sizeof(initPair_v2));

    // In fixed-V2 mode, keep re-offering the init every ~800ms until the MCU
    // ACKs. Under auto-detect the outer state machine drives one attempt per
    // protocol per cycle, so don't self-repeat here (that would monopolize V2
    // and never give V1 a turn).
    if (!self->is_auto_detect()) {
      App.scheduler.set_timeout(self, "v2_init_repeat", 800,
                                [self]() { self->performHandshakeStep(); });
    }
    return;
  }

  // Step ≥ 1: MCU has ACK'd — stop init bursts.
  // v2_on_message handles the rest (E1/A0 queries from MCU).
}

static bool v2_is_status_response(uint8_t* data, size_t len) {
  // MAD50PS1QWT-A status frames are 36 bytes with three discriminators,
  // all sharing the same byte layout (power@11, mode@12, fan@13, target@17,
  // humidity@26, temp@27, error@31):
  //   0x05/0xA0 — seed status sent once at end of handshake
  //   0x03/0xC8 — polled status (reply to the 0x41 status query)
  //   0x02/0xC8 — push-on-change status (physical button press)
  if (len <= 10) return false;
  if (data[9] == 0x05 && data[10] == 0xA0) return true;
  if (data[10] == 0xC8 && (data[9] == 0x02 || data[9] == 0x03)) return true;
  return false;
}

static bool v2_on_message(MideaDehumComponent* self, uint8_t* data, size_t len) {
  if (len < 10) return false;

  // Device ACK during broadcast (step 0) → send dongleInfo + acquiring network status
  if (data[9] == 0x07 && self->get_handshake_step() == 0) {
    self->set_appliance_type(data[2]);
    self->set_mcu_protocol_version(data[8]);  // byte[8] = protocol version (0x08=V2, 0x00=V1)
    self->set_device_info_known(true);
    ESP_LOGD(TAG, "V2 ACK received, sending 3a+3b(acquiring)");
    self->write_array(dongleInfoResponse_v2, sizeof(dongleInfoResponse_v2));
    self->write_array(netStatusAcquiring_v2, sizeof(netStatusAcquiring_v2));
    self->set_handshake_step(1);
    return true;
  }

  // E1 info query → send WiFi network status (no IP yet)
  if (data[9] == 0xE1) {
    ESP_LOGD(TAG, "V2 E1 query, sending NetStatus WiFi");
    self->write_array(netStatusWifi_v2, sizeof(netStatusWifi_v2));
    return true;
  }

  // 0xA0 response → send WiFi+IP twice (counter 02 then 03). MCU emits the
  // seed status after the second one.
  if (data[9] == 0xA0) {
    ESP_LOGD(TAG, "V2 0xA0, sending NetStatus WiFi+IP (02, 03)");
    self->write_array(netStatusWifiIp02_v2, sizeof(netStatusWifiIp02_v2));
    self->write_array(netStatusWifiIp_v2, sizeof(netStatusWifiIp_v2));
    return true;
  }

  // Network status request (0x63 at pos 9) → MCU is polling the dongle for
  // its network state. Reply with the 0x63-typed net-status frame (matching
  // the factory dongle) so the MCU accepts it and proceeds to stream status.
  if (data[9] == 0x63) {
    ESP_LOGD(TAG, "V2 0x63 net-status request, sending NetStatus (0x63)");
    self->write_array(netStatus63_v2, sizeof(netStatus63_v2));
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

  // [1] Power: bit0 = power (1=ON), bit1 = fixed, bit6 (0x40) = beep-on-command.
  // Base is 0x03/0x02 (beep off); the 0x40 beep bit is added only when the
  // "Beep on Command" switch is on. The factory 0x43/0x42 had beep always set.
  cmd[1] = s.powerOn ? 0x03 : 0x02;
#ifdef USE_MIDEA_DEHUM_BEEP
  if (self->get_beep_state()) cmd[1] |= 0x40;
#endif

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
