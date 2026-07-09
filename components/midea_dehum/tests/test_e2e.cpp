// Category 3: End-to-End Simulations
//
// Simulates real-world usage patterns through the ESPHome public interface.
// Each test: boot → handshake → command(s) → MCU feedback → verify state.
//
// Tests:
//   3.1  Cold boot → handshake → status (covered by test_handshake 1.1)
//   3.2  Power cycle: OFF → ON → MCU acknowledges
//   3.3  Mode switch: Setpoint → Continuous → Smart → verify preset names
//   3.4  Fan ramp: Low → Medium → High → Low
//   3.5  Humidity change: set 35% → MCU confirms 35%
//   3.6  Push notification: MCU sends status while idle → UI updates
//   3.7  Concurrent changes: toggle power + mode + fan in one control() call
//   3.8  50-frame soak: feed many status frames → no memory growth, no drift

#include "fixtures.h"

// ── Helper: complete the V1 handshake, returns the device ─────────────────
static void complete_handshake(TestMideaDehum& dev) {
  dev.setup();
  run_scheduler();
  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  run_scheduler();
  dev.rx_enqueue(V1_A0_RESPONSE, sizeof(V1_A0_RESPONSE));
  dev.loop();
  run_scheduler();
  dev.rx_enqueue(V1_PING, sizeof(V1_PING));
  dev.loop();
  run_scheduler();
  // Feed an initial status so we have a known baseline
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
}

// ══════════════════════════════════════════════════════════════════════════
//  3.2  Power cycle: OFF → ON → MCU acknowledges ON
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_power_cycle() {
  TestMideaDehum dev;
  complete_handshake(dev);
  size_t tx_before = dev.uart_.tx_count();

  // User turns power ON
  dev.cmd_power(true);
  ASSERT(dev.pub_power(), "published power switches to ON");
  ASSERT(dev.uart_.tx_count() > tx_before, "command TX sent for power ON");

  // MCU responds with a status confirming power ON
  tx_before = dev.uart_.tx_count();
  dev.rx_enqueue(V1_STATUS_ON, sizeof(V1_STATUS_ON));
  dev.loop();
  dev.print_state();
  ASSERT(dev.pub_power(), "MCU confirms power ON");
  ASSERT_EQ(dev.raw_mode(), 1, "MCU reports mode=1 (setpoint)");
  ASSERT_EQ(dev.raw_setpoint(), 60, "MCU setpoint now 60%");

  // User turns power OFF
  tx_before = dev.uart_.tx_count();
  dev.cmd_power(false);
  ASSERT(!dev.pub_power(), "published power switches to OFF");

  // MCU confirms OFF
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
  ASSERT(!dev.pub_power(), "MCU confirms power OFF");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.3  Mode switch: Setpoint → Continuous → Smart
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_mode_switch() {
  TestMideaDehum dev;
  complete_handshake(dev);

  // Switch to Setpoint mode
  dev.cmd_mode(1);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Setpoint",
         "preset switches to Setpoint");

  // Switch to Continuous mode
  dev.cmd_mode(2);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Continuous",
         "preset switches to Continuous");

  // Switch to Smart (default)
  dev.cmd_mode(3);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Smart",
         "preset switches to Smart");

  // Switch to ClothesDrying
  dev.cmd_mode(4);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "ClothesDrying",
         "preset switches to ClothesDrying");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.4  Fan ramp: Low → Medium → High → Low
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_fan_ramp() {
  TestMideaDehum dev;
  complete_handshake(dev);

  dev.cmd_fan(esphome::climate::CLIMATE_FAN_LOW);
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_LOW, "fan LOW published");

  dev.cmd_fan(esphome::climate::CLIMATE_FAN_MEDIUM);
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_MEDIUM, "fan MEDIUM published");

  dev.cmd_fan(esphome::climate::CLIMATE_FAN_HIGH);
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_HIGH, "fan HIGH published");

  dev.cmd_fan(esphome::climate::CLIMATE_FAN_LOW);
  ASSERT_EQ(dev.pub_fan(), (int)esphome::climate::CLIMATE_FAN_LOW, "fan back to LOW");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.5  Humidity change: set 35% → MCU confirms via status response
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_humidity_change() {
  TestMideaDehum dev;
  complete_handshake(dev);

  // User sets humidity to 35%
  dev.cmd_humidity(35.0f);
  ASSERT_EQ((int)dev.pub_target_hum(), 35, "published target humidity 35%");

  // MCU responds with a status that has setpoint=35
  uint8_t status35[] = {
      0xAA, 0x23, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0A, 0xC8,
      0x00, 0x03, 0x3C, 0x7F, 0x7F, 0x00, 0x23, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2D, 0x5F,
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  dev.rx_enqueue(status35, sizeof(status35));
  dev.loop();
  ASSERT_EQ(dev.raw_setpoint(), 35, "MCU confirms setpoint=35%");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.6  Push notification: MCU sends status while idle → UI updates
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_push_notification() {
  TestMideaDehum dev;
  complete_handshake(dev);

  // Baseline: OFF, mode 3, humidity 45
  ASSERT(!dev.pub_power(), "baseline: power OFF");
  ASSERT_EQ(dev.raw_humidity(), 45, "baseline: humidity 45%");

  // MCU pushes a status frame (e.g., user pressed the physical humidity+ button)
  // This should update state without any polling
  dev.rx_enqueue(V1_STATUS_ON, sizeof(V1_STATUS_ON));
  dev.loop();
  dev.print_state();

  ASSERT(dev.pub_power(), "push: power now ON (button press)");
  ASSERT_EQ(dev.raw_mode(), 1, "push: mode=1 (physically changed)");
  ASSERT_EQ(dev.raw_humidity(), 55, "push: humidity now 55%");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.7  Concurrent changes: power + mode + fan in one control() call
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_concurrent() {
  TestMideaDehum dev;
  complete_handshake(dev);

  // Turn ON, set mode=1(Setpoint), fan=HIGH in a single ClimateCall
  esphome::climate::ClimateCall call;
  call.mode_     = esphome::climate::CLIMATE_MODE_DRY;
  call.preset_   = "Setpoint";
  call.fan_mode_ = esphome::climate::CLIMATE_FAN_HIGH;
  dev.control(call);

  ASSERT(dev.pub_power(), "concurrent: power ON");
  ASSERT(dev.pub_fan() == (int)esphome::climate::CLIMATE_FAN_HIGH, "concurrent: fan HIGH");
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Setpoint",
         "concurrent: preset Setpoint");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.8  Soak test: 50 status frames → no drift, no crash
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_soak() {
  TestMideaDehum dev;
  complete_handshake(dev);

  for (int i = 0; i < 50; i++) {
    dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
    dev.loop();
  }

  // After 50 frames, state should still match V1_STATUS (OFF, mode=3, fan=60, etc.)
  ASSERT(!dev.pub_power(), "soak: power still OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "soak: mode still 3");
  ASSERT_EQ(dev.raw_humidity(), 45, "soak: humidity still 45%");
  ASSERT(dev.uart_.tx_count() < 500, "soak: TX count bounded (no infinite loop)");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.9  Scheduler cascade: verify handshake timeouts chain without loops
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_scheduler_cascade() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();  // drains startup delay

  // After setup, the dongle announce was sent (step 0)
  // The MCU ACK triggers a scheduler timeout (handshake_step_1, 200ms)
  // which queues another action. Verify the chain works.
  size_t initial_tx = dev.uart_.tx_count();

  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  // Before run_scheduler, the ACK has been processed but the timeout
  // hasn't fired yet
  run_scheduler();  // fires handshake_step_1 timeout → sends 0xA0 status

  ASSERT(dev.uart_.tx_count() > initial_tx, "scheduler cascade: additional TX after timeout");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.10  V2 50-frame soak: feed many V2 status frames → no drift, no crash
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_v2_soak() {
  TestMideaDehum dev;
  complete_v2_handshake(dev);

  for (int i = 0; i < 50; i++) {
    dev.rx_enqueue(V2_STATUS, sizeof(V2_STATUS));
    dev.loop();
  }

  ASSERT(!dev.pub_power(), "V2 soak: power still OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "V2 soak: mode still 3");
  ASSERT_EQ(dev.raw_humidity(), 45, "V2 soak: humidity still 45%%");
  ASSERT(dev.uart_.tx_count() < 500, "V2 soak: TX count bounded (no infinite loop)");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.11  V2 Power cycle: OFF → ON → MCU acknowledges
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_v2_power_cycle() {
  TestMideaDehum dev;
  complete_v2_handshake(dev);
  size_t tx_before = dev.uart_.tx_count();

  // User turns power ON
  dev.cmd_power(true);
  ASSERT(dev.pub_power(), "V2: published power switches to ON");
  ASSERT(dev.uart_.tx_count() > tx_before, "V2: command TX sent for power ON");

  // MCU responds with a status confirming power ON
  tx_before = dev.uart_.tx_count();
  dev.rx_enqueue(V2_STATUS_ON, sizeof(V2_STATUS_ON));
  dev.loop();
  ASSERT(dev.pub_power(), "V2: MCU confirms power ON");
  ASSERT_EQ(dev.raw_mode(), 1, "V2: MCU reports mode=1 (setpoint)");
  ASSERT_EQ(dev.raw_setpoint(), 60, "V2: MCU setpoint now 60%%");

  // User turns power OFF
  tx_before = dev.uart_.tx_count();
  dev.cmd_power(false);
  ASSERT(!dev.pub_power(), "V2: published power switches to OFF");

  // MCU confirms OFF
  dev.rx_enqueue(V2_STATUS, sizeof(V2_STATUS));
  dev.loop();
  ASSERT(!dev.pub_power(), "V2: MCU confirms power OFF");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.12  V2 Mode switch: Setpoint → Continuous → Smart → ClothesDrying
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_v2_mode_switch() {
  TestMideaDehum dev;
  complete_v2_handshake(dev);

  dev.cmd_mode(1);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Setpoint",
         "V2: preset switches to Setpoint");

  dev.cmd_mode(2);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Continuous",
         "V2: preset switches to Continuous");

  dev.cmd_mode(3);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Smart",
         "V2: preset switches to Smart");

  dev.cmd_mode(4);
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "ClothesDrying",
         "V2: preset switches to ClothesDrying");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.13  V2 Push notification: unsolicited ON status updates state
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_v2_push_notification() {
  TestMideaDehum dev;
  complete_v2_handshake(dev);

  // Baseline: OFF, mode 3, humidity 45
  ASSERT(!dev.pub_power(), "V2 baseline: power OFF");

  // MCU pushes an ON status (physical button press on the appliance)
  dev.rx_enqueue(V2_STATUS_ON, sizeof(V2_STATUS_ON));
  dev.loop();

  ASSERT(dev.pub_power(), "V2 push: power now ON (button press)");
  ASSERT_EQ(dev.raw_mode(), 1, "V2 push: mode=1 (physically changed)");
  ASSERT_EQ(dev.raw_humidity(), 55, "V2 push: humidity now 55%%");
}

// ══════════════════════════════════════════════════════════════════════════
//  3.14  V2 Concurrent changes: power + mode + fan in one control() call
// ══════════════════════════════════════════════════════════════════════════

static void test_e2e_v2_concurrent() {
  TestMideaDehum dev;
  complete_v2_handshake(dev);

  esphome::climate::ClimateCall call;
  call.mode_     = esphome::climate::CLIMATE_MODE_DRY;
  call.preset_   = "Setpoint";
  call.fan_mode_ = esphome::climate::CLIMATE_FAN_HIGH;
  dev.control(call);

  ASSERT(dev.pub_power(), "V2 concurrent: power ON");
  ASSERT(dev.pub_fan() == (int)esphome::climate::CLIMATE_FAN_HIGH, "V2 concurrent: fan HIGH");
  ASSERT(dev.pub_preset() && std::string(dev.pub_preset()) == "Setpoint",
         "V2 concurrent: preset Setpoint");
}

// ══════════════════════════════════════════════════════════════════════════
//  Runner
// ══════════════════════════════════════════════════════════════════════════

#ifndef TEST_COMBINED
int main() {
  printf("Category 3: End-to-End Simulations\n");
  printf("==================================\n");

  int total = 0;
  total += run_test("3.2  Power cycle", test_e2e_power_cycle);
  total += run_test("3.3  Mode switch", test_e2e_mode_switch);
  total += run_test("3.4  Fan ramp", test_e2e_fan_ramp);
  total += run_test("3.5  Humidity change", test_e2e_humidity_change);
  total += run_test("3.6  Push notification", test_e2e_push_notification);
  total += run_test("3.7  Concurrent changes", test_e2e_concurrent);
  total += run_test("3.8  50-frame soak", test_e2e_soak);
  total += run_test("3.9  Scheduler cascade", test_e2e_scheduler_cascade);
  total += run_test("3.10 V2 50-frame soak", test_e2e_v2_soak);

  if (total == 0) {
    printf("\n✓ All E2E tests passed!\n");
  } else {
    printf("\n✗ %d E2E test(s) failed\n", total);
  }
  return total > 0 ? 1 : 0;
}
#endif
