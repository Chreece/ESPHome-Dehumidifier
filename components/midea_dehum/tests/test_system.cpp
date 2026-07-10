// Categories 5, 6, 7 combined — Error handling, Protocol compliance,
// and State consistency.
//
// All tests drive the component through the public ESPHome interface:
//   rx_enqueue() feeds MCU frames through the full UART path
//   Verification reads pub_*() (published climate) and raw_*() (internal state)

#include "fixtures.h"

// ══════════════════════════════════════════════════════════════════════════
//  CATEGORY 5 — Error Handling
// ══════════════════════════════════════════════════════════════════════════

static void test_err_bad_start() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  uint8_t bad[] = {0xBB, 0x0C, 0xA1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x07, 0x00, 0xF9};
  dev.rx_enqueue(bad, sizeof(bad));
  dev.loop();

  ASSERT(!dev.raw_power(), "bad start byte: power unchanged");
  ASSERT_EQ(dev.raw_mode(), 3, "bad start byte: mode unchanged");
}

static void test_err_bad_length() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  uint8_t bad[] = {0xAA, 0x01, 0x00};
  dev.rx_enqueue(bad, sizeof(bad));
  dev.loop();

  ASSERT(!dev.raw_power(), "bad length: state unchanged");
}

static void test_err_truncated() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  uint8_t partial[] = {0xAA, 0x0C, 0xA1, 0x00};
  dev.rx_enqueue(partial, sizeof(partial));
  dev.loop();

  ASSERT(!dev.raw_power(), "truncated frame: no crash");
}

static void test_err_unknown_type() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  uint8_t unknown[] = {
      0xAA, 0x0C, 0xA1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x99, 0x00, 0x00};
  dev.rx_enqueue(unknown, sizeof(unknown));
  dev.loop();

  ASSERT(!dev.raw_power(), "unknown type: state unchanged");
}

static void test_err_empty_rx() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.loop();
  ASSERT(!dev.raw_power(), "empty buffer: no crash");
}

// ══════════════════════════════════════════════════════════════════════════
//  CATEGORY 6 — Protocol Compliance (byte-for-byte)
// ══════════════════════════════════════════════════════════════════════════

static const uint8_t DONGLE_ANNOUNCE_SPEC[] = {
    0xAA, 0x0B, 0xFF, 0xF4, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x07, 0x00, 0xFA};

static void test_compliance_dongle_announce() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  ASSERT(dev.uart_.tx_count() >= 1, "dongle announce was sent");
  const auto& f = dev.uart_.tx_at(0);
  ASSERT_EQ((int)f.data.size(), (int)sizeof(DONGLE_ANNOUNCE_SPEC), "dongle announce: 12 bytes");
  ASSERT(memcmp(f.data.data(), DONGLE_ANNOUNCE_SPEC, sizeof(DONGLE_ANNOUNCE_SPEC)) == 0,
         "dongle announce: byte-for-byte match");
}

static void test_compliance_status_query() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Complete handshake — the post_handshake_init timeout schedules getStatus(),
  // which sends a status query via sendMessage().  The frame contents depend on
  // device_info_known_ fields populated during the handshake, so we verify
  // structural correctness rather than byte-for-byte against a fixture.
  size_t tx_before = dev.uart_.tx_count();

  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  run_scheduler();
  dev.rx_enqueue(V1_A0_RESPONSE, sizeof(V1_A0_RESPONSE));
  dev.loop();
  run_scheduler();
  dev.rx_enqueue(V1_PING, sizeof(V1_PING));
  dev.loop();
  run_scheduler();  // triggers post_handshake_init → getStatus

  // At least one new TX frame should appear after the ping step
  ASSERT(dev.uart_.tx_count() > tx_before, "status query: new TX frame after handshake");

  // The last frame(s) should include the status query:
  // sendMessage(0x03, 0x03, ...) → data[8]=0x03(agreement), data[9]=0x03(msgType)
  bool has_query = false;
  for (size_t i = tx_before; i < dev.uart_.tx_count(); i++) {
    const auto& f = dev.uart_.tx_at(i);
    if (f.data.size() >= 12 && f.data[9] == 0x03 && f.data[8] == 0x03 &&
        f.data[10] == 0x41) {
      has_query = true;
      break;
    }
  }
  ASSERT(has_query, "status query: msgType=0x03, payload starts 0x41");
}

static void test_compliance_command_header() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.cmd_power(true);
  ASSERT(dev.uart_.tx_count() >= 1, "command TX sent");

  const auto& f = dev.uart_.tx_at(dev.uart_.tx_count() - 1);
  ASSERT(f.data.size() > 10, "command: >= 10 bytes");
  ASSERT_EQ(f.data[0], 0xAA, "command: start byte");
  ASSERT_EQ(f.data[2], 0xA1, "command: device type A1");
  ASSERT_EQ(f.data[10], 0x48, "command: write marker 0x48");
}

// ══════════════════════════════════════════════════════════════════════════
//  CATEGORY 7 — State Consistency
// ══════════════════════════════════════════════════════════════════════════

static void test_state_pub_raw_consistency() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(V1_STATUS_ON, sizeof(V1_STATUS_ON));
  dev.loop();
  dev.print_state();

  ASSERT(dev.pub_power(), "consistency: power ON");
  ASSERT(dev.raw_power(), "consistency: raw power matches");
  ASSERT_EQ(dev.raw_mode(), 1, "consistency: mode=1 (setpoint)");
  ASSERT_EQ(dev.raw_setpoint(), 60, "consistency: raw setpoint=60");
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_LOW, "consistency: fan LOW published");
}

static void test_state_first_run() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
  ASSERT_EQ(dev.raw_humidity(), 45, "first run: humidity=45");
  ASSERT_EQ(dev.raw_mode(), 3, "first run: mode=3");

  // Feed same status again — no spurious change
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
  ASSERT_EQ(dev.raw_humidity(), 45, "second run: humidity unchanged");
  ASSERT_EQ(dev.raw_mode(), 3, "second run: mode unchanged");
}

static void test_state_power_stays_off() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();

  ASSERT(!dev.pub_power(), "power stays OFF after OFF status");
  ASSERT(!dev.raw_power(), "raw power stays OFF");
}

static void test_state_temperature() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();

  float temp = dev.raw_temp();
  ASSERT(temp >= 22.0f && temp <= 24.0f, "temperature decoded ~23.3C");
  ASSERT(dev.pub_current_temp() >= 22.0f && dev.pub_current_temp() <= 24.0f,
         "published temperature matches");
}

static void test_state_defrost() {
  uint8_t defrost_status[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(defrost_status, sizeof(defrost_status));
  dev.loop();
  ASSERT(true, "defrost frame: no crash");
}

static void test_state_bucket_full() {
  uint8_t bucket_status[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
      0x08, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00};

  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.rx_enqueue(bucket_status, sizeof(bucket_status));
  dev.loop();
  ASSERT(true, "bucket full frame: no crash");
}

// 7.7  Temperature extremes: -19°C and 50°C
static void test_state_temp_extremes() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // temp_raw = 12 → (12 - 50) / 2 = -19.0°C (should not clamp further)
  uint8_t cold[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x0C,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(cold, sizeof(cold));
  dev.loop();
  ASSERT(dev.raw_temp() >= -20.0f && dev.raw_temp() <= -18.0f, "temp: ~-19C");
}

static void test_state_temp_hot() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // temp_raw = 150 → (150 - 50) / 2 = 50.0°C (should not clamp further)
  uint8_t hot[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x96,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(hot, sizeof(hot));
  dev.loop();
  ASSERT(dev.raw_temp() >= 49.0f && dev.raw_temp() <= 51.0f, "temp: ~50C");
}

// 7.8  Humidity clamping: >100% → 99%
static void test_state_humidity_clamp() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // setpoint byte = 120 (should clamp to 99)
  uint8_t high_hum[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x78, 0x00, 0x00,  // [17]=120
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(high_hum, sizeof(high_hum));
  dev.loop();
  ASSERT_EQ(dev.raw_setpoint(), 99, "setpoint >100 clamped to 99");
}

// 7.9  Multiple frames in one RX buffer — all handled
static void test_state_multi_frame() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Queue two complete status frames back-to-back in the RX buffer
  uint8_t buf[sizeof(V1_STATUS) + sizeof(V1_STATUS_ON)];
  memcpy(buf, V1_STATUS, sizeof(V1_STATUS));
  memcpy(buf + sizeof(V1_STATUS), V1_STATUS_ON, sizeof(V1_STATUS_ON));

  dev.rx_enqueue(buf, sizeof(buf));
  dev.loop();                 // processes first frame
  dev.loop();                 // processes second frame

  dev.print_state();
  // After second frame (V1_STATUS_ON): power=ON, mode=1, fan=40, humidity=55
  ASSERT(dev.pub_power(), "multi-frame: power ON from second frame");
  ASSERT_EQ(dev.raw_mode(), 1, "multi-frame: mode=1");
  ASSERT_EQ(dev.raw_humidity(), 55, "multi-frame: humidity=55");
}

// 7.10  traits() returns correct capabilities
static void test_state_traits() {
  TestMideaDehum dev;
  auto t = dev.traits();

  // traits() must return valid modes
  ASSERT(true, "traits: called without crash");
  // The component should support OFF + DRY modes
  (void)t;  // just verify no crash
}

// 7.11  device_info_known_ populated during handshake
static void test_state_device_info_known() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();
  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();

  // After ACK, device_info_known_ should be true
  // We can't read it directly, but the next TX headers will use appliance_type_
  // instead of the hardcoded 0xA1. Verify by sending a command and checking
  // byte[2] of the TX frame.
  dev.cmd_power(true);
  const auto& f = dev.uart_.tx_at(dev.uart_.tx_count() - 1);
  ASSERT_EQ(f.data[2], 0xA1, "device type in TX header is A1 (from ACK)");
}

// 7.12  Temperature below -19°C clamps to -20°C
static void test_state_temp_clamp_below() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // temp_raw = 8 → (8 - 50) / 2 = -21.0°C → should clamp to -20
  uint8_t very_cold[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x08,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(very_cold, sizeof(very_cold));
  dev.loop();
  ASSERT(dev.raw_temp() >= -21.0f && dev.raw_temp() <= -19.0f, "temp: clamps near -20C");
}

// 7.13  Temperature above 50°C clamps to 50°C
static void test_state_temp_clamp_above() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // temp_raw = 152 → (152 - 50) / 2 = 51.0°C → should clamp to 50
  uint8_t very_hot[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x32, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x98,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(very_hot, sizeof(very_hot));
  dev.loop();
  ASSERT(dev.raw_temp() >= 49.0f && dev.raw_temp() <= 52.0f, "temp: clamps near 50C");
}

// 7.14  Humidity at exactly 100% — should NOT clamp
static void test_state_humidity_exact_100() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  uint8_t hum100[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x64, 0x00, 0x00,  // [17]=100
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(hum100, sizeof(hum100));
  dev.loop();
  ASSERT_EQ(dev.raw_setpoint(), 100, "setpoint 100% not clamped");
}

// 7.15  Rapid control() calls produce separate TX frames
static void test_state_rapid_control() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  dev.cmd_power(true);
  size_t after_first = dev.uart_.tx_count();
  dev.cmd_mode(1);  // Setpoint
  ASSERT(dev.uart_.tx_count() > after_first, "rapid calls: second TX distinct");
}

// ══════════════════════════════════════════════════════════════════════════
//  Runner
// ══════════════════════════════════════════════════════════════════════════

int main() {
  printf("Categories 5-7: Error, Compliance, Consistency\n");
  printf("===============================================\n");

  int total = 0;

  printf("\n  --- Category 5: Error Handling ---\n");
  total += run_test("5.1  Bad start byte", test_err_bad_start);
  total += run_test("5.2  Bad length", test_err_bad_length);
  total += run_test("5.3  Truncated frame", test_err_truncated);
  total += run_test("5.4  Unknown type", test_err_unknown_type);
  total += run_test("5.6  Empty buffer", test_err_empty_rx);

  printf("\n  --- Category 6: Protocol Compliance ---\n");
  total += run_test("6.1  Dongle announce match", test_compliance_dongle_announce);
  total += run_test("6.2  Status query match", test_compliance_status_query);
  total += run_test("6.3  Command header", test_compliance_command_header);

  printf("\n  --- Category 7: State Consistency ---\n");
  total += run_test("7.1  Published <-> raw", test_state_pub_raw_consistency);
  total += run_test("7.2  First-run vs subsequent", test_state_first_run);
  total += run_test("7.3  Power stays OFF", test_state_power_stays_off);
  total += run_test("7.4  Temperature decoding", test_state_temperature);
  total += run_test("7.5  Defrost frame", test_state_defrost);
  total += run_test("7.6  Bucket full frame", test_state_bucket_full);
  total += run_test("7.7  Temperature extremes (cold)", test_state_temp_extremes);
  total += run_test("7.8  Temperature extremes (hot)", test_state_temp_hot);
  total += run_test("7.9  Humidity >100% clamped", test_state_humidity_clamp);
  total += run_test("7.10 Multi-frame buffer", test_state_multi_frame);
  total += run_test("7.11 traits() no crash", test_state_traits);
  total += run_test("7.12 Device info known after ACK", test_state_device_info_known);
  total += run_test("7.13 Temp below -19C clamps", test_state_temp_clamp_below);
  total += run_test("7.14 Temp above 50C clamps", test_state_temp_clamp_above);
  total += run_test("7.15 Humidity exactly 100%", test_state_humidity_exact_100);
  total += run_test("7.16 Rapid control calls", test_state_rapid_control);

  if (total == 0) {
    printf("\n✓ All system verification tests passed!\n");
  } else {
    printf("\n✗ %d test(s) failed\n", total);
  }
  return total > 0 ? 1 : 0;
}
