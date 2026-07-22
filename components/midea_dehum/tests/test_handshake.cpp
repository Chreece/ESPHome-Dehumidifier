// Category 1: Handshake Tests (V1 only)
//
// Verifies the opening handshake through the ESPHome public interface.
// All test methods use only:
//   dev.setup(), dev.loop(), dev.rx_enqueue(), run_scheduler()
//   dev.pub_*() and dev.raw_*() for verification

#include "fixtures.h"

// ══════════════════════════════════════════════════════════════════════════
//  1.1  V1 Handshake — full sequence
// ══════════════════════════════════════════════════════════════════════════

static void test_v1_handshake() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  printf("  Step 0: Dongle announce\n");
  ASSERT(dev.uart_.tx_count() >= 1, "dongle announce sent after setup()");

  // MCU ACK
  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  run_scheduler();
  printf("  Step 1: MCU ACK\n");
  ASSERT(dev.uart_.tx_count() >= 2, "network status sent after ACK");

  // 0xA0
  dev.rx_enqueue(V1_A0_RESPONSE, sizeof(V1_A0_RESPONSE));
  dev.loop();
  run_scheduler();
  printf("  Step 2: 0xA0 response\n");
  ASSERT(dev.uart_.tx_count() >= 3, "connected status sent after 0xA0");

  // Ping → handshake done
  dev.rx_enqueue(V1_PING, sizeof(V1_PING));
  dev.loop();
  run_scheduler();
  printf("  Step 3: UART ping\n");
  ASSERT(dev.uart_.tx_count() >= 5, "ping echoed + status query sent");

  // Status response
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
  printf("  Step 4: Status parsed\n");
  dev.print_state();

  ASSERT(!dev.pub_power(), "published: power is OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "mode is 3");
  ASSERT_EQ(dev.raw_fan(), 60, "fan is 60");
  ASSERT_EQ(dev.raw_setpoint(), 50, "setpoint is 50");
  ASSERT_EQ(dev.raw_humidity(), 45, "humidity is 45");
  ASSERT(dev.raw_temp() > 22.0f && dev.raw_temp() < 24.0f, "temperature ~23.3C");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.2  Handshake disabled
// ══════════════════════════════════════════════════════════════════════════

static void test_handshake_disabled() {
  TestMideaDehum dev;
  dev.set_handshake_enabled(false);
  dev.setup();
  run_scheduler();

  // With handshake disabled + USE_MIDEA_DEHUM_HANDSHAKE defined,
  // setup() skips the handshake entirely (no TX sent).
  // The code sets handshake_step=2 + handshake_done=true without sending.
  // This is the expected behavior — no frames are generated.

  // Status should still be parseable even without handshake
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();
  printf("  Status parsed without handshake\n");
  ASSERT_EQ(dev.raw_humidity(), 45, "status parsed without handshake");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.3  Status arriving before handshake completes
// ══════════════════════════════════════════════════════════════════════════

static void test_handshake_early_status() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Inject a status frame mid-handshake — should still parse and set handshake_done
  dev.rx_enqueue(V1_STATUS, sizeof(V1_STATUS));
  dev.loop();

  printf("  Early status during handshake\n");
  dev.print_state();
  ASSERT_EQ(dev.raw_humidity(), 45, "status parsed during handshake");
  ASSERT_EQ(dev.raw_mode(), 3, "mode correctly read");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.4  V2 Handshake — full sequence via inject()
// ══════════════════════════════════════════════════════════════════════════

static void test_v2_handshake() {
  TestMideaDehum dev;
  dev.set_protocol_version(2);
  dev.set_handshake_enabled(true);
  dev.setup();
  run_scheduler();

  size_t tx_count = dev.uart_.tx_count();
  printf("  Step 1: V2 init burst sent\n");
  ASSERT(tx_count >= 1, "TX frame count >= 1 after V2 setup");

  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
  run_scheduler();
  printf("  Step 2: Received V2 ACK, sent dongleInfo + acquiring status\n");
  ASSERT(dev.uart_.tx_count() >= tx_count + 2, "dongleInfo + acquiring sent");
  tx_count = dev.uart_.tx_count();

  dev.inject(V2_E1_QUERY, sizeof(V2_E1_QUERY));
  printf("  Step 3: Received E1 query, sent WiFi network status\n");
  ASSERT(dev.uart_.tx_count() > tx_count, "new TX after E1");
  tx_count = dev.uart_.tx_count();

  dev.inject(V2_A0_RESPONSE, sizeof(V2_A0_RESPONSE));
  printf("  Step 4: Received 0xA0, sent 2x WiFi+IP status\n");
  ASSERT(dev.uart_.tx_count() >= tx_count + 2, "2x WiFi+IP frames sent");

  dev.inject(V2_STATUS, sizeof(V2_STATUS));
  dev.print_state();
  printf("  Step 5: Parsed V2 status\n");
  ASSERT(!dev.raw_power(), "V2 power is OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "V2 mode is 3");
  ASSERT_EQ(dev.raw_fan(), 40, "V2 fan speed is 40 (0x28=Low)");
  ASSERT_EQ(dev.raw_setpoint(), 50, "V2 setpoint is 50%%");
  ASSERT_EQ(dev.raw_humidity(), 45, "V2 humidity is 45%%");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.5  V2 status arriving mid-handshake → sets handshake_done
// ══════════════════════════════════════════════════════════════════════════

static void test_v2_early_status() {
  TestMideaDehum dev;
  dev.set_protocol_version(2);
  dev.setup();
  run_scheduler();

  // Inject a V2 status frame during the init-burst handshake phase
  // This should set handshake_done immediately, stopping further bursts
  dev.inject(V2_STATUS, sizeof(V2_STATUS));
  dev.loop();

  ASSERT(dev.is_handshake_done(), "V2 early status: handshake_done set");
  dev.print_state();
  ASSERT(!dev.raw_power(), "V2 early status: power OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "V2 early status: mode=3");
  ASSERT_EQ(dev.raw_humidity(), 45, "V2 early status: humidity=45%%");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.6  V1 seed-status-as-ping — MCU sends 0x05/0xA0 during handshake
//       Some devices (Midea Cube 35, Comfee MDDF series) emit a seed-
//       status frame (0x05/0xA0) during the V1 handshake instead of a
//       separate 0xA0 response + 0x05 ping.  V1 treats 0x05 as a UART
//       ping — it echoes the frame and marks handshake done.  The status
//       data in the frame is NOT parsed; the first real status comes
//       from the scheduled poll 1500ms later.
//       Regression test for issue #45.
// ══════════════════════════════════════════════════════════════════════════

static void test_v1_seed_status_ping() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Step 0: dongle announce sent
  ASSERT(dev.uart_.tx_count() >= 1, "step 0: dongle announce sent");

  // MCU ACK
  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  run_scheduler();
  ASSERT(dev.is_device_info_known(), "device info known after ACK");

  // Instead of a separate 0xA0 response, the MCU sends a single 0x05/0xA0
  // seed-status frame (agreement version 0x03, like @dnakamura's Cube 35).
  // V1's is_status_response checks data[10]==0xC8, and 0xA0 ≠ 0xC8, so
  // this lands in v1_on_message which treats data[9]==0x05 as a UART ping.
  dev.rx_enqueue(V1_3_SEED_STATUS, sizeof(V1_3_SEED_STATUS));
  dev.loop();
  run_scheduler();

  printf("  After V1.3 seed-status-as-ping\n");
  ASSERT(dev.is_handshake_done(), "handshake done (ping echo + post_handshake_init scheduled)");

  // The seed-status payload was NOT parsed as status data (V1's is_status_response
  // rejected it), so state remains at compile-time defaults.
  ASSERT(!dev.raw_power(), "seed status: power still default OFF");
  ASSERT_EQ(dev.raw_mode(), 3, "seed status: mode still default 3");

  // The post_handshake_init timeout schedules getStatus() in 1500ms.
  // Feed the polled status response to verify the full cycle works.
  dev.rx_enqueue(V1_3_POLL_STATUS, sizeof(V1_3_POLL_STATUS));
  dev.loop();
  dev.print_state();

  ASSERT(dev.raw_power(), "poll status: power now ON (from poll response)");
  ASSERT_EQ(dev.raw_mode(), 2, "poll status: mode=2 (Continuous)");
  ASSERT_EQ(dev.raw_setpoint(), 50, "poll status: setpoint=50%%");
  ASSERT_EQ(dev.raw_humidity(), 41, "poll status: humidity=41%%");
  ASSERT(dev.raw_temp() >= 21.5f && dev.raw_temp() <= 22.5f,
         "poll status: temperature ~22.0C");
}

// ══════════════════════════════════════════════════════════════════════════
//  1.7  V1.3 agreement-version handshake — full cycle with 0x03 agreement
//       Verifies the complete handshake→poll cycle for devices that use
//       agreement version 0x03 (Midea Cube 35, Comfee MDDF series).
//       Regression test for issue #45.
// ══════════════════════════════════════════════════════════════════════════

static void test_v1_3_full_cycle() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // V1.3 ACK
  dev.rx_enqueue(V1_3_DEVICE_ACK, sizeof(V1_3_DEVICE_ACK));
  dev.loop();
  run_scheduler();
  ASSERT(dev.is_device_info_known(), "V1.3 ACK: device info known");

  // V1.3 seed status (0x05/0xA0) — V1 treats as ping, echoes back, sets handshake_done
  dev.rx_enqueue(V1_3_SEED_STATUS, sizeof(V1_3_SEED_STATUS));
  dev.loop();
  run_scheduler();
  ASSERT(dev.is_handshake_done(), "V1.3: handshake done after seed status echo");

  // Verify adaptive query uses the ACK's mcu_protocol_version (0x00 for V1.3 ACK)
  ASSERT_EQ((int)dev.get_mcu_protocol_version(), 0, "V1.3: mcu_protocol_version = 0x00");

  // Feed the polled status response
  dev.rx_enqueue(V1_3_POLL_STATUS, sizeof(V1_3_POLL_STATUS));
  dev.loop();
  dev.print_state();

  ASSERT(dev.raw_power(), "V1.3 poll: power ON");
  ASSERT_EQ(dev.raw_mode(), 2, "V1.3 poll: mode=2 (Continuous)");
  ASSERT_EQ(dev.raw_humidity(), 41, "V1.3 poll: humidity=41%%");

  // Now feed a push-on-change (physical button press)
  dev.rx_enqueue(V1_3_PUSH_STATUS, sizeof(V1_3_PUSH_STATUS));
  dev.loop();
  dev.print_state();

  ASSERT(dev.raw_power(), "V1.3 push: power still ON");
  ASSERT_EQ(dev.raw_mode(), 1, "V1.3 push: mode changed to 1 (Setpoint via physical button)");
  ASSERT_EQ(dev.raw_setpoint(), 60, "V1.3 push: setpoint now 60%%");
  ASSERT_EQ(dev.raw_humidity(), 55, "V1.3 push: humidity now 55%%");
}

// ══════════════════════════════════════════════════════════════════════════
//  Runner
// ══════════════════════════════════════════════════════════════════════════

#ifndef TEST_COMBINED
int main() {
  printf("Category 1: Handshake Tests (V1)\n");
  printf("================================\n");

  int total = 0;
  total += run_test("1.1  V1 handshake", test_v1_handshake);
  total += run_test("1.2  Handshake disabled", test_handshake_disabled);
  total += run_test("1.3  Early status during handshake", test_handshake_early_status);
  total += run_test("1.4  V2 handshake", test_v2_handshake);
  total += run_test("1.5  V2 early status", test_v2_early_status);
  total += run_test("1.6  V1 seed-status-as-ping (V1.3)", test_v1_seed_status_ping);
  total += run_test("1.7  V1.3 agreement full cycle", test_v1_3_full_cycle);

  if (total == 0) {
    printf("\n✓ All handshake tests passed!\n");
  } else {
    printf("\n✗ %d handshake test(s) failed\n", total);
  }
  return total > 0 ? 1 : 0;
}
#endif
