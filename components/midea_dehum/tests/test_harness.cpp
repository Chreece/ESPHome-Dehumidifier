// Combined test runner — dispatches to individual test suites.
//
// This file #includes all individual test .cpp files into a single
// translation unit.  The TEST_COMBINED macro suppresses main() in
// the included files.
//
// Compile:
//   cd tests && make
//
// Run:
//   ./test_harness v1              # V1: handshake + commands + E2E + system
//   ./test_harness v2              # V2: handshake + commands + E2E + system
//   ./test_harness all             # everything including auto-detect

#define TEST_COMBINED  // suppress main() in included test .cpp files

#include "fixtures.h"

// ── Include all test suites ──────────────────────────────────────────────
#include "test_handshake.cpp"
#include "test_commands.cpp"
#include "test_e2e.cpp"
#include "test_system.cpp"

// ══════════════════════════════════════════════════════════════════════════
//  Auto-detect tests (not a standalone executable, only compiled when
//  MIDEA_PROTOCOL_AUTO is defined)
// ══════════════════════════════════════════════════════════════════════════

#ifdef MIDEA_PROTOCOL_AUTO

static void test_ad_init() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  run_scheduler();

  // After setup with protocol_version=0, ad_try_start should kick in
  // and schedule the first auto-detect round. Verify no crash.
  ASSERT(true, "auto-detect init: no crash after setup");
}

static void test_ad_response_detection() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  run_scheduler_once();  // start first auto-detect round (V1)

  // Feed a V1 ACK — should trigger ad_on_ack → switch_protocol → ad.active=false
  dev.inject(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));

  ASSERT(dev.ad_state_.active == false, "auto-detect: locked in after V1 ACK (active=false)");
  ASSERT(dev.get_protocol_ptr()->version == 1, "auto-detect: protocol locked to V1");
}

static void test_ad_round_progression() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  run_scheduler_once();  // start first auto-detect round (V1)

  // Feed a V1 ACK — should trigger ad_on_ack → switch_protocol → lock in
  dev.inject(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));

  // After locking in, further calls to protocol_auto_next should be no-ops
  dev.protocol_auto_next();
  run_scheduler_once();
  ASSERT(dev.ad_state_.active == false, "auto-detect: locked in after V1 ACK");
  ASSERT(dev.get_protocol_ptr()->version == 1, "auto-detect: protocol version is V1");
}

static void test_ad_v2_ack_detection() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  run_scheduler_once();  // start first auto-detect round (V1)

  // Feed a V2 ACK (byte[8]=0x08 marks it as V2) — should trigger ad_on_ack
  // → switch_protocol(&PROTOCOL_V2) → ad.active=false
  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));

  ASSERT(dev.ad_state_.active == false, "auto-detect V2: locked in after V2 ACK (active=false)");
  ASSERT(dev.get_protocol_ptr()->version == 2, "auto-detect: protocol locked to V2");
}

#endif  // MIDEA_PROTOCOL_AUTO

// ══════════════════════════════════════════════════════════════════════════
//  Protocol switching test
// ══════════════════════════════════════════════════════════════════════════

static void test_switch_protocol() {
  TestMideaDehum dev;
  dev.setup();
  run_scheduler();

  // Start with V1, complete handshake
  dev.rx_enqueue(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  dev.loop();
  run_scheduler();

  // Switch to V2 — should reset handshake and start V2 init bursts
  dev.switch_protocol(&esphome::midea_dehum::PROTOCOL_V2);
  run_scheduler();  // fires switch_protocol_init → performHandshakeStep → V2 burst

  // After switch, V2 init burst should have been sent
  size_t tx_after_switch = dev.uart_.tx_count();
  ASSERT(tx_after_switch > 0, "switch: TX sent after switching to V2");

  // Verify V2 handshake can complete
  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
  dev.inject(V2_E1_QUERY, sizeof(V2_E1_QUERY));
  dev.inject(V2_A0_RESPONSE, sizeof(V2_A0_RESPONSE));
  dev.inject(V2_STATUS, sizeof(V2_STATUS));
  dev.loop();
  run_scheduler();

  ASSERT(dev.is_handshake_done(), "switch: V2 handshake_done after switch");
  ASSERT(!dev.raw_power(), "switch: state intact after protocol switch");
}

// ══════════════════════════════════════════════════════════════════════════
//  Main dispatcher
// ══════════════════════════════════════════════════════════════════════════

#include <cstring>

int main(int argc, char** argv) {
  const char* mode = (argc >= 2) ? argv[1] : "v1";

  printf("ESPHome Dehumidifier Test Harness\\n");
  printf("==================================\\n");

  if (strcmp(mode, "all") == 0 || strcmp(mode, "v1") == 0 ||
      strcmp(mode, "v2") == 0) {

    int total = 0;

    printf("\\n=== Category 1: Handshake Tests ===\\n");
    total += run_test("1.1  V1 handshake", test_v1_handshake);
    total += run_test("1.2  Handshake disabled", test_handshake_disabled);
    total += run_test("1.3  Early status during handshake", test_handshake_early_status);
    total += run_test("1.4  V2 handshake", test_v2_handshake);
    total += run_test("1.5  V2 early status", test_v2_early_status);

    printf("\\n=== Category 2: Command Tests ===\\n");
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
    total += run_test("2.12  V2 Power ON/OFF", test_v2_cmd_power);
    total += run_test("2.13  V2 Mode presets", test_v2_cmd_modes);
    total += run_test("2.14  V2 Fan speed", test_v2_cmd_fan);
    total += run_test("2.15  V2 Target humidity", test_v2_cmd_humidity);
#ifdef USE_MIDEA_DEHUM_PUMP
    total += run_test("2.16  V2 Pump", test_v2_cmd_pump);
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
    total += run_test("2.17  V2 Filter cleaned flag", test_v2_cmd_filter_cleaned);
#endif
    total += run_test("2.18  V2 Water level threshold", test_v2_cmd_water_level);
#ifdef USE_MIDEA_DEHUM_TIMER
    total += run_test("2.19  V2 Timer echo", test_v2_cmd_timer_echo);
    total += run_test("2.20  V2 Timer set", test_v2_cmd_timer_set);
#endif
#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
    total += run_test("2.21  V2 Reset water level", test_v2_cmd_reset_water_level);
#endif

    printf("\\n=== Category 3: E2E Tests ===\\n");
    total += run_test("3.2  Power cycle", test_e2e_power_cycle);
    total += run_test("3.3  Mode switch", test_e2e_mode_switch);
    total += run_test("3.4  Fan ramp", test_e2e_fan_ramp);
    total += run_test("3.5  Humidity change", test_e2e_humidity_change);
    total += run_test("3.6  Push notification", test_e2e_push_notification);
    total += run_test("3.7  Concurrent changes", test_e2e_concurrent);
    total += run_test("3.8  50-frame soak", test_e2e_soak);
    total += run_test("3.9  Scheduler cascade", test_e2e_scheduler_cascade);
    total += run_test("3.10 V2 50-frame soak", test_e2e_v2_soak);
    total += run_test("3.11 V2 Power cycle", test_e2e_v2_power_cycle);
    total += run_test("3.12 V2 Mode switch", test_e2e_v2_mode_switch);
    total += run_test("3.13 V2 Push notification", test_e2e_v2_push_notification);
    total += run_test("3.14 V2 Concurrent changes", test_e2e_v2_concurrent);

    printf("\\n=== Categories 5-7: Error, Compliance, Consistency ===\\n");
    total += run_test("5.1  Bad start byte", test_err_bad_start);
    total += run_test("5.2  Bad length", test_err_bad_length);
    total += run_test("5.3  Truncated frame", test_err_truncated);
    total += run_test("5.4  Unknown type", test_err_unknown_type);
    total += run_test("5.5  V2 truncated frame", test_err_v2_truncated);
    total += run_test("5.6  Empty buffer", test_err_empty_rx);
    total += run_test("5.7  Net-status request response", test_err_net_status_request);
    total += run_test("6.1  Dongle announce match", test_compliance_dongle_announce);
    total += run_test("6.2  Status query match", test_compliance_status_query);
    total += run_test("6.3  Command header", test_compliance_command_header);
    total += run_test("6.4  CRC/checksum verification", test_compliance_crc);
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
#ifdef USE_MIDEA_DEHUM_TIMER
    total += run_test("7.17 Timer parsing in status", test_state_timer_parsing);
#endif
    total += run_test("8.1  V2 status parsing", test_v2_state_parsing);
    total += run_test("8.2  V2 status ON+high fan", test_v2_state_on_high);
    total += run_test("8.3  V2 status humidity 35%", test_v2_state_hum35);

#ifdef MIDEA_PROTOCOL_AUTO
    if (strcmp(mode, "all") == 0) {
      printf("\\n=== Auto-Detect Protocol Tests ===\\n");
      total += run_test("AD.1  Init", test_ad_init);
      total += run_test("AD.2  V1 ACK detection", test_ad_response_detection);
      total += run_test("AD.3  V1 lock-in after ACK", test_ad_round_progression);
      total += run_test("AD.4  V2 ACK detection", test_ad_v2_ack_detection);
    }
#endif

    total += run_test("SW.1  Switch protocol V1→V2", test_switch_protocol);

    if (total == 0) {
      printf("\\n✓ All tests passed!\\n");
    } else {
      printf("\\n✗ %d test(s) failed\\n", total);
    }
    return total > 0 ? 1 : 0;
  }

  printf("Usage: %s [v1|v2|all]\\n", argv[0]);
  return 1;
}
