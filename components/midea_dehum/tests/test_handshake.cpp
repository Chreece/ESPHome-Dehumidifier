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
//  Runner
// ══════════════════════════════════════════════════════════════════════════

int main() {
  printf("Category 1: Handshake Tests (V1)\n");
  printf("================================\n");

  int total = 0;
  total += run_test("1.1  V1 handshake", test_v1_handshake);
  total += run_test("1.2  Handshake disabled", test_handshake_disabled);
  total += run_test("1.3  Early status during handshake", test_handshake_early_status);

  if (total == 0) {
    printf("\n✓ All handshake tests passed!\n");
  } else {
    printf("\n✗ %d handshake test(s) failed\n", total);
  }
  return total > 0 ? 1 : 0;
}
