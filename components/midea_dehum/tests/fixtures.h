// Shared test infrastructure — TestUART, TestMideaDehum, assertion macros,
// and known V1 + V2 MCU frame data.
//
// All tests drive the component through the public ESPHome interface:
//   setup(), loop(), control(), set_handshake_enabled(), etc.
// Verification reads published climate fields and optionally internal state
// through subclass accessor methods.
//
// Usage:
//   #include "fixtures.h"
//   // test functions here ...
//   #ifndef TEST_COMBINED
//   int main() { ... }
//   #endif

#pragma once

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "midea_dehum.h"

// ══════════════════════════════════════════════════════════════════════════
//  TestUART — captures TX, replays RX through handleUart()
// ══════════════════════════════════════════════════════════════════════════

struct CapturedFrame {
  std::vector<uint8_t> data;
};

class TestUART : public esphome::uart::UARTComponent {
 public:
  std::vector<CapturedFrame> tx_frames;

  std::vector<uint8_t> rx_queue;
  size_t rx_pos{0};

  void load_rx(const uint8_t* data, size_t len) {
    rx_queue.assign(data, data + len);
    rx_pos = 0;
  }

  bool read_byte(uint8_t* out) override {
    if (rx_pos < rx_queue.size()) {
      *out = rx_queue[rx_pos++];
      return true;
    }
    return false;
  }
  int available() override { return static_cast<int>(rx_queue.size() - rx_pos); }

  void write_array(const uint8_t* data, size_t len) override {
    CapturedFrame f;
    f.data.assign(data, data + len);
    tx_frames.push_back(std::move(f));
  }

  // ── Direct frame injection (bypasses byte-by-byte handleUart) ────

  void inject_frame(esphome::midea_dehum::MideaDehumComponent* comp,
                    const uint8_t* data, size_t len) {
    comp->processPacket(const_cast<uint8_t*>(data), len);
  }

  size_t tx_count() const { return tx_frames.size(); }
  void clear_tx() { tx_frames.clear(); }
  const CapturedFrame& tx_at(size_t i) const { return tx_frames[i]; }

  void print_tx() const {
    for (size_t i = 0; i < tx_frames.size(); i++) {
      printf("  TX[%zu] (%zu bytes): ", i, tx_frames[i].data.size());
      for (size_t j = 0; j < tx_frames[i].data.size() && j < 32; j++)
        printf("%02X ", tx_frames[i].data[j]);
      if (tx_frames[i].data.size() > 32) printf("...");
      printf("\n");
    }
  }
};

// ══════════════════════════════════════════════════════════════════════════
//  TestMideaDehum — test harness wrapping the component
// ══════════════════════════════════════════════════════════════════════════

class TestMideaDehum : public esphome::midea_dehum::MideaDehumComponent {
 public:
  TestUART uart_;

  TestMideaDehum() {
    this->set_uart(&uart_);
    this->set_protocol_version(1);  // default to V1 for most tests
    this->set_handshake_enabled(true);
  }

  // ── RX injection — feeds bytes through the full UART → processPacket path ──

  void rx_enqueue(const uint8_t* data, size_t len) { uart_.load_rx(data, len); }

  // Direct frame injection (for handshake / clean path tests — bypasses handleUart)
  void inject(const uint8_t* data, size_t len) { uart_.inject_frame(this, data, len); }

  // ── Direct state access (for sub-byte verification) ────────────────────

  const esphome::midea_dehum::DehumidifierState& get_state() const { return state_; }

  // ── Public API wrappers (simulate HA user actions via control()) ──────────

  void cmd_power(bool on) {
    esphome::climate::ClimateCall call;
    call.mode_ = on ? esphome::climate::CLIMATE_MODE_DRY
                    : esphome::climate::CLIMATE_MODE_OFF;
    this->control(call);
  }

  void cmd_mode(uint8_t m) {
    const char* presets[] = {"", "Setpoint", "Continuous", "Smart", "ClothesDrying"};
    esphome::climate::ClimateCall call;
    call.mode_   = esphome::climate::CLIMATE_MODE_DRY;
    call.preset_ = presets[m];
    this->control(call);
  }

  void cmd_fan(esphome::climate::ClimateFanMode fm) {
    esphome::climate::ClimateCall call;
    call.mode_     = esphome::climate::CLIMATE_MODE_DRY;
    call.fan_mode_ = fm;
    this->control(call);
  }

  void cmd_humidity(float pct) {
    esphome::climate::ClimateCall call;
    call.mode_            = esphome::climate::CLIMATE_MODE_DRY;
    call.target_humidity_ = pct;
    this->control(call);
  }

  void cmd_swing(esphome::climate::ClimateSwingMode sm) {
    esphome::climate::ClimateCall call;
    call.mode_       = esphome::climate::CLIMATE_MODE_DRY;
    call.swing_mode_ = sm;
    this->control(call);
  }

  // ── Published climate state (public ESPHome interface) ────────────────────

  bool        pub_power()          const { return this->mode != esphome::climate::CLIMATE_MODE_OFF; }
  int         pub_fan()            const { return static_cast<int>(this->fan_mode); }
  int         pub_swing()          const { return static_cast<int>(this->swing_mode); }
  float       pub_target_hum()     const { return this->target_humidity; }
  float       pub_current_hum()    const { return this->current_humidity; }
  float       pub_current_temp()   const { return this->current_temperature; }
  const char* pub_preset()         const { return this->custom_preset_ptr_; }

  // ── Internal state (accessed via protected state_ in subclass) ────────────

  bool    raw_power()     const { return state_.powerOn; }
  uint8_t raw_mode()      const { return state_.mode; }
  uint8_t raw_fan()       const { return state_.fanSpeed; }
  uint8_t raw_setpoint()  const { return state_.humiditySetpoint; }
  uint8_t raw_humidity()  const { return state_.currentHumidity; }
  float   raw_temp()      const { return state_.currentTemperature; }
#ifdef USE_MIDEA_DEHUM_DEFROST
  bool    raw_defrost()   const { return this->defrost_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  bool    raw_bucket_full() const { return this->bucket_full_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_ERROR
  uint8_t raw_error()     const { return this->error_state_; }
#endif
  bool    is_handshake_done() const { return this->handshake_done_; }

  void print_state() const {
    printf("  pub:  power=%d fan=%d swing=%d hum=%.0f/%.0f temp=%.1f preset=%s\n",
           pub_power(), pub_fan(), pub_swing(),
           pub_target_hum(), pub_current_hum(), pub_current_temp(),
           pub_preset() ? pub_preset() : "(none)");
    printf("  raw:  power=%d mode=%u fan=%u set=%u%% hum=%u%% temp=%.1f\n",
           raw_power(), raw_mode(), raw_fan(),
           raw_setpoint(), raw_humidity(), raw_temp());
  }

  // ── Feature setters (used by V2 command tests to override internal state) ──

#ifdef USE_MIDEA_DEHUM_PUMP
  void set_pump_state(bool on) { this->pump_state_ = on; sendSetStatus(); }
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  void set_sleep_state(bool on){ this->sleep_state_ = on; sendSetStatus(); }
#endif
#ifdef USE_MIDEA_DEHUM_BEEP
  void set_beep_state(bool on) { this->beep_state_  = on; sendSetStatus(); }
#endif
#ifdef USE_MIDEA_DEHUM_ION
  void set_ion_state(bool on)  { this->ion_state_  = on; sendSetStatus(); }
#endif
};

// ══════════════════════════════════════════════════════════════════════════
//  Assertions
// ══════════════════════════════════════════════════════════════════════════

static int failures = 0;

#define PASS(msg) printf("  PASS: %s\n", msg)
#define FAIL(msg) do { printf("  FAIL: %s\n", msg); failures++; } while (0)

#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } else { PASS(msg); } } while (0)
#define ASSERT_EQ(a, b, msg) do { \
  auto _a = (a); auto _b = (b); \
  if (_a != _b) { printf("  FAIL: %s (got %d, expected %d)\n", msg, (int)_a, (int)_b); failures++; } \
  else { PASS(msg); } \
} while (0)

inline int run_test(const char* label, std::function<void()> fn) {
  reset_scheduler();  // prevent stale callbacks from previous tests
  failures = 0;
  printf("\n=== %s ===\n", label);
  fn();
  printf("=== %s: %d failures ===\n", label, failures);
  return failures;
}

// ══════════════════════════════════════════════════════════════════════════
//  V1 MCU frames — known-good captures from the original implementation
// ══════════════════════════════════════════════════════════════════════════

static const uint8_t V1_DEVICE_ACK[] = {0xAA, 0x0B, 0xA1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x07, 0x00, 0x53};

static const uint8_t V1_A0_RESPONSE[] = {0xAA, 0x16, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xA0, 0x41, 0x00, 0xFF, 0x03, 0xFF, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};

static const uint8_t V1_PING[] = {0xAA, 0x0A, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x5A};

// OFF, mode=3, fan=60, set=50%, humidity=45%, temp=23.3°C
static const uint8_t V1_STATUS[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8, 0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE, 0xA9};

// ON, mode=1(set), fan=40, set=60%, humidity=55%, temp=23.3°C
static const uint8_t V1_STATUS_ON[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8, 0x01, 0x01, 0x28, 0x7F, 0x7F, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xAB};

// ══════════════════════════════════════════════════════════════════════════
//  V2 MCU frames — known-good captures from MAD50PS1QWT-A
// ══════════════════════════════════════════════════════════════════════════

static const uint8_t V2_DEVICE_ACK[] = {0xAA, 0x29, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

static const uint8_t V2_E1_QUERY[] = {0xAA, 0x1A, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xE1, 0x81, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint8_t V2_A0_RESPONSE[] = {0xAA, 0x2A, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xA0, 0x00, 0xA1, 0x03, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD3};

// V2 status — OFF, mode=3, fan=0x28=40, set=50%, humidity=45%, temp=23.3C
static const uint8_t V2_STATUS[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0xA0, 0x00, 0x03, 0x28, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0};

// V2 status ON, mode=1(setpoint), fan=0x28=40, set=60%, humidity=55%, temp=23.3C
static const uint8_t V2_STATUS_ON[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0xA0, 0x01, 0x01, 0x28, 0x7F, 0x7F, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8D};

// V2 status ON, mode=3(smart), fan=0xD0=HIGH, set=50%, humidity=45%, temp=23.3C
static const uint8_t V2_STATUS_ON_HIGH[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0xA0, 0x01, 0x03, 0xD0, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7};

// V2 status with humidity setpoint 35%
static const uint8_t V2_STATUS_HUM35[] = {0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0xA0, 0x00, 0x03, 0x28, 0x7F, 0x7F, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAF};

// ══════════════════════════════════════════════════════════════════════════
//  Shared helpers
// ══════════════════════════════════════════════════════════════════════════

// Extract the 25-byte command payload from a sendMessage frame
// sendMessage wraps: [AA LL ... 10-byte header][25-byte payload][CRC][checksum]
inline const uint8_t* cmd_payload(const CapturedFrame& f) {
  return &f.data[10];
}

inline void tx_clear(TestMideaDehum& dev) { dev.uart_.clear_tx(); }

inline const CapturedFrame& tx_last(TestMideaDehum& dev, const char* label) {
  if (dev.uart_.tx_count() < 1) {
    printf("  WARN: %s: no TX frames captured\n", label);
    static CapturedFrame empty;
    return empty;
  }
  return dev.uart_.tx_at(dev.uart_.tx_count() - 1);
}

// Complete the V2 handshake — sets protocol, injects ACK/E1/A0/status
inline void complete_v2_handshake(TestMideaDehum& dev) {
  dev.set_protocol_version(2);
  dev.set_handshake_enabled(true);
  dev.setup();
  run_scheduler();  // fires startup timer → init burst + scheduler spins 20x

  // Inject handshake frames directly (no run_scheduler to avoid more bursts)
  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
  dev.inject(V2_E1_QUERY, sizeof(V2_E1_QUERY));
  dev.inject(V2_A0_RESPONSE, sizeof(V2_A0_RESPONSE));

  // Feed status — this sets handshake_done, stopping future bursts
  dev.inject(V2_STATUS, sizeof(V2_STATUS));
  dev.loop();

  // Drain any leftover scheduler timeouts (they return immediately now
  // because handshake_done is true)
  run_scheduler();
}
