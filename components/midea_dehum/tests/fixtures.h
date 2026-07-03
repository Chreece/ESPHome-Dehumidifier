// Shared test infrastructure — TestUART, TestMideaDehum, assertion macros,
// and known V1 MCU frame data.
//
// All tests drive the component through the public ESPHome interface:
//   setup(), loop(), control(), set_handshake_enabled(), etc.
// Verification reads published climate fields and optionally internal state
// through subclass accessor methods.

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

  TestMideaDehum() { this->set_uart(&uart_); }

  // ── RX injection — feeds bytes through the full UART → processPacket path ──

  void rx_enqueue(const uint8_t* data, size_t len) { uart_.load_rx(data, len); }

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

  void print_state() const {
    printf("  pub:  power=%d fan=%d swing=%d hum=%.0f/%.0f temp=%.1f preset=%s\n",
           pub_power(), pub_fan(), pub_swing(),
           pub_target_hum(), pub_current_hum(), pub_current_temp(),
           pub_preset() ? pub_preset() : "(none)");
    printf("  raw:  power=%d mode=%u fan=%u set=%u%% hum=%u%% temp=%.1f\n",
           raw_power(), raw_mode(), raw_fan(),
           raw_setpoint(), raw_humidity(), raw_temp());
  }
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
  failures = 0;
  printf("\n=== %s ===\n", label);
  fn();
  printf("=== %s: %d failures ===\n", label, failures);
  return failures;
}

// ══════════════════════════════════════════════════════════════════════════
//  V1 MCU frames — known-good captures from the original implementation
// ══════════════════════════════════════════════════════════════════════════

static const uint8_t V1_DEVICE_ACK[] = {
    0xAA, 0x0C, 0xA1, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x07, 0x00, 0xF9};

static const uint8_t V1_A0_RESPONSE[] = {
    0xAA, 0x17, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xA0,
    0x41, 0x00, 0xFF, 0x03, 0xFF, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x54};

static const uint8_t V1_PING[] = {
    0xAA, 0x0A, 0xA1, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0xF6};

// OFF, mode=3, fan=60, set=50%, humidity=45%, temp=23.3°C
static const uint8_t V1_STATUS[] = {
    0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
    0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE, 0xE8};

// ON, mode=1(set), fan=40, set=60%, humidity=55%, temp=23.3°C
static const uint8_t V1_STATUS_ON[] = {
    0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
    0x01, 0x01, 0x28, 0x7F, 0x7F, 0x00, 0x3C, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x5F,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xE8};
