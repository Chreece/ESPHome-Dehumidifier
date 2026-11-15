#include "fixtures.h"

static size_t discovery_queries(const TestMideaDehum& dev) {
  size_t count = 0;
  for (const auto& frame : dev.uart_.tx_frames)
    if (frame.data.size() > 10 && frame.data[9] == 0x03 && frame.data[10] == 0x41) ++count;
  return count;
}

static void test_discovery_watchdog_after_fallback() {
  // Include uint32_t rollover and an ACK whose lock timestamp is exactly zero.
  for (uint64_t start : {uint64_t(0), uint64_t(UINT32_MAX) - 200, uint64_t(UINT32_MAX) - 99}) {
    reset_scheduler();
    test_time_ms = start;
    TestMideaDehum dev;
    dev.set_protocol_version(0);
    dev.set_status_poll_interval(1);
    dev.setup();
    advance_time(100);  // first discovery probe
    dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
    dev.loop();
    ASSERT(dev.ad_state_.lock_pending, "ACK creates an unconfirmed lock, even at millis zero");
    ASSERT(!dev.is_status_polling_allowed(), "ACK alone does not open poll gate");

    advance_time(14999);
    dev.loop();
    ASSERT(!dev.is_status_polling_allowed(), "poll gate stays closed before 15s");
    ASSERT_EQ(discovery_queries(dev), size_t(0), "no early status query during handshake");

    advance_time(1);
    dev.loop();
    ASSERT(dev.is_status_polling_allowed(), "15s fallback permits polling");
    ASSERT(!dev.is_handshake_done(), "fallback does not complete handshake");
    ASSERT(!dev.ad_state_.got_response, "fallback does not invent a status response");
    advance_time(1);
    dev.loop();  // allow one configured poll interval, independent of other tests
    ASSERT(discovery_queries(dev) > 0, "fallback actually transmits a status query");

    advance_time(14998);
    ASSERT(!dev.is_auto_detect(), "watchdog does not restart before 30s");
    advance_time(1);
    ASSERT(dev.is_auto_detect(), "30s watchdog resumes discovery despite fallback");
    ASSERT(!dev.is_status_polling_allowed(), "watchdog immediately closes polling");
    size_t queries = discovery_queries(dev);
    dev.loop();
    advance_time(99);
    dev.loop();
    ASSERT_EQ(discovery_queries(dev), queries, "restart gap stays quiet");
    advance_time(1);
    ASSERT_EQ(dev.get_protocol_ptr()->version, 1, "restart begins with a new V1 probe");
    ASSERT(!dev.is_status_polling_allowed(), "new probe does not inherit fallback permission");

    // The next ACK must get its own full grace period.
    dev.inject(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
    advance_time(14999);
    dev.loop();
    ASSERT(!dev.is_status_polling_allowed(), "new lock starts a fresh fallback timer");
    advance_time(1);
    dev.loop();
    ASSERT(dev.is_status_polling_allowed(), "new lock reaches its own 15s fallback");
    ASSERT(!dev.ad_state_.got_response, "new lock still requires real status");
  }
}

static void test_discovery_status_confirms_lock() {
  for (bool after_fallback : {false, true}) {
    TestMideaDehum dev;
    dev.set_protocol_version(0);
    dev.setup();
    advance_time(100);
    dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
    advance_time(after_fallback ? 20000 : 14000);
    dev.loop();
    ASSERT_EQ(dev.is_status_polling_allowed(), after_fallback, "fallback follows its deadline");
    ASSERT(!dev.ad_state_.got_response, "no status confirmation before receiving a frame");
    // Exercise both seed and polled status paths.
    std::vector<uint8_t> status(V2_STATUS_ON, V2_STATUS_ON + sizeof(V2_STATUS_ON));
    if (after_fallback) { status[9] = 0x03; status[10] = 0xC8; }
    dev.inject(status.data(), status.size());
    ASSERT(dev.ad_state_.got_response, "received status confirms selected protocol");
    ASSERT(!dev.ad_state_.lock_pending, "received status clears pending lock");
    ASSERT(dev.is_handshake_done(), "real status completes handshake");
    advance_time(after_fallback ? 10000 : 16000);  // exactly 30s after ACK
    ASSERT(!dev.is_auto_detect(), "confirmed protocol is not abandoned by watchdog");
    ASSERT_EQ(dev.get_protocol_ptr()->version, 2, "confirmed V2 selection retained");
  }
}

static void test_discovery_commands_during_fallback() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  advance_time(100);
  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
  advance_time(15000);
  dev.loop();
  size_t queries = discovery_queries(dev);
  dev.cmd_power(true);
  ASSERT_EQ(discovery_queries(dev), queries + 1, "deferred command can request status during fallback");
  for (const auto& frame : dev.uart_.tx_frames)
    ASSERT(frame.data.size() <= 10 || frame.data[10] != 0x48, "no default-based settings sent during fallback");
  dev.inject(V2_STATUS, sizeof(V2_STATUS));
  const CapturedFrame& command = tx_last(dev, "fallback startup command");
  ASSERT(command.data.size() > 17, "first status flushes pending command");
  if (command.data.size() > 17) {
    ASSERT_EQ(command.data[10], 0x48, "pending settings emitted after first status");
    ASSERT_EQ(command.data[11] & 1, 1, "pending power ON survives device OFF status");
    ASSERT_EQ(command.data[17], 50, "unrequested device setpoint preserved");
  }
  advance_time(15000);
  ASSERT(!dev.is_auto_detect(), "status from fallback query prevents watchdog recovery");
}

static void test_discovery_ping_is_not_status() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  advance_time(100);
  dev.inject(V1_DEVICE_ACK, sizeof(V1_DEVICE_ACK));
  advance_time(100);  // selected V1's startup announcement
  dev.inject(V1_PING, sizeof(V1_PING));
  ASSERT(dev.is_handshake_done(), "V1 ping completes scripted handshake as before");
  ASSERT(!dev.ad_state_.got_response, "V1 ping is not a parsed device status");
  advance_time(29900);  // 30s after ACK
  ASSERT(dev.is_auto_detect(), "watchdog still recovers a V1 lock with no status");
  ASSERT(!dev.is_status_polling_allowed(), "recovery closes even a ping-opened poll gate");
}

static void test_discovery_disabled_fallback_watchdog() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.set_handshake_fallback_ms(0);
  dev.setup();
  advance_time(100);
  dev.inject(V2_DEVICE_ACK, sizeof(V2_DEVICE_ACK));
  advance_time(15000);
  dev.loop();
  ASSERT(!dev.is_status_polling_allowed(), "timeout zero disables timed fallback");
  ASSERT_EQ(discovery_queries(dev), size_t(0), "timeout zero sends no status query");
  advance_time(15000);
  ASSERT(dev.is_auto_detect(), "timeout zero does not disable lock watchdog");
}

static void test_fixed_fallback_and_protocol_switch() {
  TestMideaDehum dev;
  dev.set_protocol_version(2);
  dev.setup();
  advance_time(15000);
  dev.loop();
  ASSERT(dev.is_status_polling_allowed(), "fixed V2 fallback permits polling without ACK");
  ASSERT(!dev.is_handshake_done(), "fixed V2 fallback does not claim completion");
  size_t frames = dev.uart_.tx_count();
  advance_time(800);
  ASSERT_EQ(dev.uart_.tx_count(), frames, "V2 init retries stop during fallback polling");

  dev.switch_protocol(&esphome::midea_dehum::PROTOCOL_V1);
  size_t queries = discovery_queries(dev);
  dev.loop();
  advance_time(14999);
  dev.loop();
  ASSERT(!dev.is_status_polling_allowed(), "protocol switch clears old fallback and resets grace period");
  ASSERT_EQ(discovery_queries(dev), queries, "switched protocol sends no early status query");
  advance_time(1);
  dev.loop();
  ASSERT(dev.is_status_polling_allowed(), "switched protocol reaches a fresh fallback deadline");
}

static int run_discovery_tests() {
  int total = 0;
  total += run_test("DR.1 ACK -> fallback -> watchdog", test_discovery_watchdog_after_fallback);
  total += run_test("DR.2 Real status confirms lock", test_discovery_status_confirms_lock);
  total += run_test("DR.3 Startup command during fallback", test_discovery_commands_during_fallback);
  total += run_test("DR.4 Ping is not a status confirmation", test_discovery_ping_is_not_status);
  total += run_test("DR.5 Disabled fallback retains watchdog", test_discovery_disabled_fallback_watchdog);
  total += run_test("DR.6 Fixed fallback and protocol switch", test_fixed_fallback_and_protocol_switch);
  return total;
}

#ifndef TEST_COMBINED
int main() { return run_discovery_tests() > 0 ? 1 : 0; }
#endif
