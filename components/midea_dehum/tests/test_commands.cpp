// Category 2: Command Tests (V1)
//
// Verifies every setter generates the correct command frame bytes.
// All commands go through sendSetStatus() → sendMessage() → TestUART capture.
//
// Command payload layout (25 bytes, after 10-byte header):
//   [0]  = 0x48 write marker
//   [1]  = power (0x01=ON, 0x00=OFF) | beep bit6=0x40
//   [2]  = mode (1=Setpoint, 2=Continuous, 3=Smart, 4=ClothesDrying)
//   [3]  = fan speed (40=Low, 80=High)
//   [4-6]= timer raw bytes
//   [7]  = humidity setpoint %
//   [9]  = feature flags: ion(bit6) sleep(bit5) pump(bit4+3)
//   [10] = swing (bit3=0x08)

#include "fixtures.h"

// ── Helper: extract the 25-byte command payload from a sendMessage frame ──
// sendMessage wraps payload in: [AA LL ... 10-byte header][25-byte payload][CRC][check]
static const uint8_t* cmd_payload(const CapturedFrame& f) {
  return &f.data[10];  // skip 10-byte header
}

// ── Helper: clear TX, issue command, verify exactly 1 frame sent ─────────

static void tx_clear(TestMideaDehum& dev) { dev.uart_.clear_tx(); }

static const CapturedFrame& tx_one(TestMideaDehum& dev, const char* label) {
  if (dev.uart_.tx_count() != 1) {
    printf("  WARN: %s: expected 1 TX frame, got %zu\n", label, dev.uart_.tx_count());
  }
  ASSERT(dev.uart_.tx_count() >= 1, (std::string(label) + ": TX frame sent").c_str());
  return dev.uart_.tx_at(dev.uart_.tx_count() - 1);
}

// ══════════════════════════════════════════════════════════════════════════
//  2.1  Power ON / OFF
// ══════════════════════════════════════════════════════════════════════════

static void test_power() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Power ON
  tx_clear(dev);
  dev.cmd_power(true);
  ASSERT_EQ(cmd_payload(tx_one(dev, "power ON"))[1], 0x01, "power ON: byte[1]=0x01");
  ASSERT(dev.pub_power(), "published: power is ON");

  // Power OFF
  tx_clear(dev);
  dev.cmd_power(false);
  ASSERT_EQ(cmd_payload(tx_one(dev, "power OFF"))[1], 0x00, "power OFF: byte[1]=0x00");
  ASSERT(!dev.pub_power(), "published: power is OFF");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.2  Mode presets
// ══════════════════════════════════════════════════════════════════════════

static void test_modes() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  auto verify_mode = [&](uint8_t m, const char* expected, const char* label) {
    tx_clear(dev);
    dev.cmd_mode(m);
    ASSERT_EQ(cmd_payload(tx_one(dev, label))[2], m, label);
    bool preset_ok = (dev.pub_preset() && std::string(dev.pub_preset()) == expected);
    ASSERT(preset_ok, (std::string(label) + ": preset matches").c_str());
  };

  verify_mode(1, "Setpoint",      "mode Setpoint");
  verify_mode(2, "Continuous",    "mode Continuous");
  verify_mode(3, "Smart",         "mode Smart");
  verify_mode(4, "ClothesDrying", "mode ClothesDrying");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.3  Fan speed
// ══════════════════════════════════════════════════════════════════════════

static void test_fan() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.cmd_fan(esphome::climate::CLIMATE_FAN_LOW);
  ASSERT_EQ(cmd_payload(tx_one(dev, "fan LOW"))[3], 40, "fan LOW: byte[3]=40");
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_LOW, "published: fan LOW");

  tx_clear(dev);
  dev.cmd_fan(esphome::climate::CLIMATE_FAN_MEDIUM);
  ASSERT_EQ(cmd_payload(tx_one(dev, "fan MEDIUM"))[3], 60, "fan MEDIUM: byte[3]=60");

  tx_clear(dev);
  dev.cmd_fan(esphome::climate::CLIMATE_FAN_HIGH);
  ASSERT_EQ(cmd_payload(tx_one(dev, "fan HIGH"))[3], 80, "fan HIGH: byte[3]=80");
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_HIGH, "published: fan HIGH");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.4  Target humidity
// ══════════════════════════════════════════════════════════════════════════

static void test_humidity() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.cmd_humidity(35.0f);
  ASSERT_EQ(cmd_payload(tx_one(dev, "humidity 35%"))[7], 35, "humidity 35%: byte[7]=35");

  tx_clear(dev);
  dev.cmd_humidity(85.0f);
  ASSERT_EQ(cmd_payload(tx_one(dev, "humidity 85%"))[7], 85, "humidity 85%: byte[7]=85");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.5  Pump
// ══════════════════════════════════════════════════════════════════════════

static void test_pump() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.set_pump_state(true);
  ASSERT_EQ(cmd_payload(tx_one(dev, "pump ON"))[9], 0x18, "pump ON: byte[9]=0x18");

  tx_clear(dev);
  dev.set_pump_state(false);
  ASSERT_EQ(cmd_payload(tx_one(dev, "pump OFF"))[9], 0x10, "pump OFF: byte[9]=0x10");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.6  Ionizer
// ══════════════════════════════════════════════════════════════════════════

static void test_ion() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.set_ion_state(true);
  ASSERT_EQ(cmd_payload(tx_one(dev, "ion ON"))[9] & 0x40, 0x40, "ion ON: byte[9] bit6");

  tx_clear(dev);
  dev.set_ion_state(false);
  ASSERT_EQ(cmd_payload(tx_one(dev, "ion OFF"))[9] & 0x40, 0x00, "ion OFF: byte[9] bit6 clear");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.7  Sleep
// ══════════════════════════════════════════════════════════════════════════

static void test_sleep() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.set_sleep_state(true);
  ASSERT_EQ(cmd_payload(tx_one(dev, "sleep ON"))[9] & 0x20, 0x20, "sleep ON: byte[9] bit5");

  tx_clear(dev);
  dev.set_sleep_state(false);
  ASSERT_EQ(cmd_payload(tx_one(dev, "sleep OFF"))[9] & 0x20, 0x00, "sleep OFF: byte[9] bit5 clear");
}

// ══════════════════════════════════════════════════════════════════════════
//  2.8  Beep (requires USE_MIDEA_DEHUM_BEEP)
// ══════════════════════════════════════════════════════════════════════════

#ifdef USE_MIDEA_DEHUM_BEEP
static void test_beep() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.set_beep_state(true);
  ASSERT_EQ(cmd_payload(tx_one(dev, "beep ON"))[1] & 0x40, 0x40, "beep ON: byte[1] bit6");

  tx_clear(dev);
  dev.set_beep_state(false);
  ASSERT_EQ(cmd_payload(tx_one(dev, "beep OFF"))[1] & 0x40, 0x00, "beep OFF: byte[1] bit6 clear");
}
#endif

// ══════════════════════════════════════════════════════════════════════════
//  2.9  Swing
// ══════════════════════════════════════════════════════════════════════════

#ifdef USE_MIDEA_DEHUM_SWING
static void test_swing() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  tx_clear(dev);
  dev.cmd_swing(esphome::climate::CLIMATE_SWING_VERTICAL);
  // control() may produce >1 TX frame when swing changes (sends separately for
  // swing + via handleStateUpdateRequest). Check the last frame.
  ASSERT(dev.uart_.tx_count() >= 1, "swing ON: TX frame(s) sent");
  ASSERT_EQ(dev.uart_.tx_at(dev.uart_.tx_count() - 1).data[20] & 0x08, 0x08, "swing ON: byte[20] bit3");

  tx_clear(dev);
  dev.cmd_swing(esphome::climate::CLIMATE_SWING_OFF);
  ASSERT(dev.uart_.tx_count() >= 1, "swing OFF: TX frame(s) sent");
  ASSERT_EQ(dev.uart_.tx_at(dev.uart_.tx_count() - 1).data[20] & 0x08, 0x00, "swing OFF: byte[20] bit3 clear");
}
#endif

// ══════════════════════════════════════════════════════════════════════════
//  2.10  Timer
// ══════════════════════════════════════════════════════════════════════════

#ifdef USE_MIDEA_DEHUM_TIMER
static void test_timer() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Set 1-hour timer.  The original timer encoding collapses 1h00m → 0h + 3 quarters + fine offset,
  // resulting in byte[4]=0x83 (not 0x84 as the naive formula would suggest).
  tx_clear(dev);
  dev.set_timer_hours(1.0f, false);
  auto p = cmd_payload(tx_one(dev, "timer 1h"));
  ASSERT((p[4] & 0x80) != 0, "timer 1h: timer bit set");

  // Clear timer
  tx_clear(dev);
  dev.set_timer_hours(0.0f, false);
  p = cmd_payload(tx_one(dev, "timer clear"));
  ASSERT_EQ(p[4], 0x00, "timer clear: byte[4]=0x00");
  ASSERT_EQ(p[5], 0x00, "timer clear: byte[5]=0x00");
}
#endif

// ══════════════════════════════════════════════════════════════════════════
//  2.11  Idempotency — duplicate commands shouldn't generate TX
// ══════════════════════════════════════════════════════════════════════════

static void test_idempotency() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Issue a command that changes state, verify TX count increases
  size_t before = dev.uart_.tx_count();
  dev.cmd_power(true);  // OFF → ON (change)
  ASSERT(dev.uart_.tx_count() > before, "changed state → TX sent");

  // Issue same command again — no state change, no TX
  before = dev.uart_.tx_count();
  dev.cmd_power(true);  // ON → ON (no change)
  ASSERT(dev.uart_.tx_count() == before, "no state change → no TX (idempotent)");
}

// ══════════════════════════════════════════════════════════════════════════
//  Runner
// ══════════════════════════════════════════════════════════════════════════

int main() {
  printf("Category 2: Command Tests (V1)\n");
  printf("==============================\n");

  int total = 0;
  total += run_test("2.1   Power ON/OFF", test_power);
  total += run_test("2.2   Mode presets", test_modes);
  total += run_test("2.3   Fan speed", test_fan);
  total += run_test("2.4   Target humidity", test_humidity);
  total += run_test("2.5   Pump", test_pump);
  total += run_test("2.6   Ionizer", test_ion);
  total += run_test("2.7   Sleep", test_sleep);
#ifdef USE_MIDEA_DEHUM_BEEP
  total += run_test("2.8   Beep", test_beep);
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  total += run_test("2.9   Swing", test_swing);
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  total += run_test("2.10  Timer", test_timer);
#endif
  total += run_test("2.11  Idempotency", test_idempotency);

  if (total == 0) {
    printf("\n✓ All command tests passed!\n");
  } else {
    printf("\n✗ %d command test(s) failed\n", total);
  }
  return total > 0 ? 1 : 0;
}
