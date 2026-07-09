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

  // Feed a V1 status — should trigger got_response=true while protocol is V1
  dev.inject(V1_STATUS, sizeof(V1_STATUS));

  ASSERT(dev.ad_state_.got_response == true, "auto-detect: got_response set after V1 status");

  // Run the next round — should lock in and clear active
  dev.protocol_auto_next();
  ASSERT(dev.ad_state_.active == false, "auto-detect: locked in (active=false)");
}

static void test_ad_round_progression() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  run_scheduler_once();  // start first auto-detect round (V1)

  // Feed a V1 status NOW while protocol is V1 → sets got_response
  dev.inject(V1_STATUS, sizeof(V1_STATUS));

  // Multiple rounds — the first protocol_auto_next should find got_response=true
  // and lock in (active=false), stopping further cycling
  for (int i = 0; i < 6; i++) {
    dev.protocol_auto_next();
    run_scheduler_once();  // drain any queued timeouts
  }
  ASSERT(dev.ad_state_.active == false, "auto-detect: locked in after response on round 2");
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

    printf("\\n=== Categories 5-7: Error, Compliance, Consistency ===\\n");
    total += run_test("5.1  Bad start byte", test_err_bad_start);
    total += run_test("5.2  Bad length", test_err_bad_length);
    total += run_test("5.3  Truncated frame", test_err_truncated);
    total += run_test("5.4  Unknown type", test_err_unknown_type);
    total += run_test("5.5  V2 truncated frame", test_err_v2_truncated);
    total += run_test("5.6  Empty buffer", test_err_empty_rx);
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

#ifdef MIDEA_PROTOCOL_AUTO
    if (strcmp(mode, "all") == 0) {
      printf("\\n=== Auto-Detect Protocol Tests ===\\n");
      total += run_test("AD.1  Init", test_ad_init);
      total += run_test("AD.2  Response detection", test_ad_response_detection);
      total += run_test("AD.3  Round progression", test_ad_round_progression);
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
