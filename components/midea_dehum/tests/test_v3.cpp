#include "fixtures.h"
#include "fixtures_v3.h"

using esphome::midea_dehum::PROTOCOL_V3;

static void test_v3_discovery() {
  for (unsigned start : {100u, 1100u, 2100u}) {
    for (bool with_ack : {false, true}) {
      reset_scheduler();
      TestMideaDehum dev;
      dev.set_protocol_version(0);
      dev.setup();
      advance_time(start);
      if (with_ack) {
        dev.rx_enqueue(V3_DEVICE_ACK, sizeof(V3_DEVICE_ACK));
        dev.loop();
        ASSERT(dev.ad_state_.lock_pending, "long ACK selects a tentative V3 lock");
        ASSERT(!dev.ad_state_.got_response, "ACK alone does not establish appliance state");
      }
      tx_clear(dev);
      dev.rx_enqueue(V3_STATUS_HUM35, sizeof(V3_STATUS_HUM35));
      dev.loop();
      ASSERT_EQ(dev.get_protocol_ptr()->version, 3, "short status identifies V3 in any probe round");
      ASSERT(dev.ad_state_.got_response && dev.is_handshake_done(), "status confirms discovery");
      ASSERT_EQ(dev.raw_setpoint(), 35, "first UART frame updates climate");
      ASSERT_EQ(dev.raw_humidity(), 58, "measured RH comes from status");
      ASSERT(!dev.uart_.tx_frames.empty() && dev.uart_.tx_at(0).data ==
             std::vector<uint8_t>(std::begin(V3_STATUS_HUM35), std::end(V3_STATUS_HUM35)),
             "seed echo uses intact UART bytes");
      advance_time(30100);
      ASSERT(!dev.is_auto_detect() && dev.get_protocol_ptr() == &PROTOCOL_V3, "lock outlives watchdog");
    }
  }
}

static void test_v3_discrimination() {
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  advance_time(100);
  uint8_t frame[sizeof(V3_FACTORY_TARGET_60_RESPONSE)];
  memcpy(frame, V3_FACTORY_TARGET_60_RESPONSE, sizeof(frame));
  frame[33]++; frame[34]--;  // keep additive sum valid, break CRC
  dev.inject(frame, sizeof(frame));
  ASSERT(!dev.ad_state_.got_response && dev.is_auto_detect(), "invalid CRC cannot identify V3");
  frame[33]--;
  dev.inject(frame, sizeof(frame));
  ASSERT(!dev.ad_state_.got_response && dev.is_auto_detect(), "invalid checksum cannot identify V3");
  dev.inject(V1_3_DEVICE_ACK, sizeof(V1_3_DEVICE_ACK));
  dev.inject(V1_3_POLL_STATUS, sizeof(V1_3_POLL_STATUS));
  ASSERT_EQ(dev.get_protocol_ptr()->version, 1, "36-byte agreement-03 status stays V1");
  dev.inject(V3_STATUS_HUM35, sizeof(V3_STATUS_HUM35));
  ASSERT_EQ(dev.get_protocol_ptr()->version, 1, "confirmed protocol stays locked");
  for (int version : {1, 2}) {
    TestMideaDehum pinned;
    pinned.set_protocol_version(version);
    pinned.inject(V3_STATUS_HUM35, sizeof(V3_STATUS_HUM35));
    ASSERT_EQ(pinned.get_protocol_ptr()->version, version, "manual V1 and V2 stay pinned");
  }
}

static void test_v3_startup() {
  TestMideaDehum dev;
  dev.set_protocol_version(3);
  dev.setup();
  ASSERT_EQ(dev.uart_.tx_count(), size_t(1), "fixed V3 announces at setup");
  dev.inject(V3_DEVICE_ACK, sizeof(V3_DEVICE_ACK));
  dev.inject(V3_DEVICE_ACK, sizeof(V3_DEVICE_ACK));
  advance_time(12720);
  ASSERT_EQ(dev.uart_.tx_count(), size_t(10), "six acquisition frames and three network messages");
  if (dev.uart_.tx_count() == 10) {
    for (unsigned i = 1; i <= 6; ++i) ASSERT_EQ(dev.uart_.tx_at(i).data[9], 0xA0, "acquisition advertisement");
    for (unsigned i = 7; i <= 9; ++i) ASSERT_EQ(dev.uart_.tx_at(i).data[9], 0x0D, "network advertisement");
  }
  tx_clear(dev);
  dev.set_pump_state(true);
  ASSERT_EQ(dev.uart_.tx_count(), size_t(0), "startup pump intent waits for first status");
  dev.rx_enqueue(V3_FACTORY_PUMP_OFF_RESPONSE, sizeof(V3_FACTORY_PUMP_OFF_RESPONSE));
  dev.loop();
  size_t writes = 0;
  for (const auto& tx : dev.uart_.tx_frames) {
    if (tx.data.size() == 34 && tx.data[10] == 0x48) {
      ++writes;
      ASSERT_EQ(tx.data[19], 0x18, "startup pump intent preserved");
    }
  }
  ASSERT_EQ(writes, size_t(1), "first C8 flushes startup command exactly once");
  ASSERT(dev.is_handshake_done(), "first command response can confirm device state");
}

static void test_v3_captured_transactions() {
  TestMideaDehum dev;
  dev.set_protocol_version(3);
  dev.set_handshake_enabled(false);
  dev.setup();
  dev.set_beep_state(true);
  dev.inject(V3_BEFORE_POWER_ON, sizeof(V3_BEFORE_POWER_ON));
  auto check = [&](const uint8_t* expected, const uint8_t* response, auto command) {
    tx_clear(dev);
    dev.seed_v3_sequence(expected[31] - 1);
    command();
    ASSERT_EQ(dev.uart_.tx_count(), size_t(1), "one command emitted");
    if (dev.uart_.tx_count() == 1)
      ASSERT(dev.uart_.tx_at(0).data == std::vector<uint8_t>(expected, expected + 34),
             "complete command equals factory capture");
    ASSERT_EQ(dev.v3_pending_sequence_, expected[31], "sequence appended before CRC");
    dev.inject(response, 35);
    ASSERT_EQ(dev.v3_pending_sequence_, -1, "matching reply acknowledged");
    ASSERT_EQ(dev.raw_setpoint(), response[17], "reported target decoded");
  };
  check(V3_FACTORY_POWER_ON, V3_FACTORY_POWER_ON_RESPONSE, [&]() { dev.cmd_power(true); });
  check(V3_FACTORY_TARGET_60, V3_FACTORY_TARGET_60_RESPONSE, [&]() { dev.cmd_humidity(60); });
  check(V3_FACTORY_TARGET_55, V3_FACTORY_TARGET_55_RESPONSE, [&]() { dev.cmd_humidity(55); });
  check(V3_FACTORY_POWER_OFF, V3_FACTORY_POWER_OFF_RESPONSE, [&]() { dev.cmd_power(false); });
  dev.inject(V3_FACTORY_PUMP_OFF_RESPONSE, sizeof(V3_FACTORY_PUMP_OFF_RESPONSE));
  check(V3_FACTORY_PUMP_ON, V3_FACTORY_PUMP_ON_RESPONSE, [&]() { dev.set_pump_state(true); });
  check(V3_FACTORY_PUMP_OFF, V3_FACTORY_PUMP_OFF_RESPONSE, [&]() { dev.set_pump_state(false); });
  dev.seed_v3_sequence(0x53);
  dev.getStatus();
  ASSERT(dev.uart_.tx_frames.back().data == std::vector<uint8_t>(std::begin(V3_FACTORY_QUERY_SEQ_54),
         std::end(V3_FACTORY_QUERY_SEQ_54)), "complete sequenced query equals factory capture");
}

static void test_v3_recovery() {
  for (uint8_t version : {1, 2}) {
    reset_scheduler();
    TestMideaDehum dev;
    dev.set_protocol_version(0);
    dev.setup();
    advance_time(2100);
    dev.inject(V3_DEVICE_ACK, sizeof(V3_DEVICE_ACK));
    ASSERT(dev.ad_state_.lock_pending, "tentative V3 lock awaits status");
    const uint8_t* legacy = version == 2 ? V2_STATUS : V1_3_POLL_STATUS;
    dev.rx_enqueue(legacy, sizeof(V2_STATUS));
    dev.loop();
    ASSERT_EQ(dev.get_protocol_ptr()->version, version, "full-size response restores legacy protocol");
    ASSERT(dev.ad_state_.got_response, "legacy reply confirms the corrected selection");
    dev.getStatus();
    ASSERT_EQ(dev.uart_.tx_frames.back().data[8], legacy[8], "poll uses the recovered legacy agreement");
    tx_clear(dev);
    advance_time(30001);
    for (const auto& tx : dev.uart_.tx_frames)
      ASSERT(tx.data.size() > 10 && tx.data[9] == 0x03 && tx.data[10] == 0xB5,
             "only the optional capability queries can follow the legacy lock");
  }
  reset_scheduler();
  TestMideaDehum dev;
  dev.set_protocol_version(0);
  dev.setup();
  advance_time(100);
  dev.inject(V3_DEVICE_ACK, sizeof(V3_DEVICE_ACK));
  advance_time(30000);
  ASSERT(dev.is_auto_detect() && !dev.ad_state_.got_response, "silent V3 ACK releases the lock");
  advance_time(120101);
  ASSERT(dev.ad_state_.failed, "silent bus finishes the bounded discovery window");
  dev.rx_enqueue(V3_FACTORY_POWER_OFF_RESPONSE, sizeof(V3_FACTORY_POWER_OFF_RESPONSE));
  dev.loop();
  ASSERT(dev.is_v3_active() && !dev.ad_state_.failed, "late valid V3 reply recovers from timeout");
}

static void test_v3_optional_controls() {
  TestMideaDehum dev;
  dev.set_protocol_version(3);
  dev.setup();
  dev.inject(V3_STATUS_SMART_LOW_TARGET_60, sizeof(V3_STATUS_SMART_LOW_TARGET_60));
  tx_clear(dev);
  dev.cmd_fan(esphome::climate::CLIMATE_FAN_MEDIUM);
  dev.cmd_mode(4);
  ASSERT_EQ(dev.raw_mode(), 3, "unsupported preset keeps Smart");
  ASSERT_EQ(dev.raw_fan(), 40, "unsupported fan keeps Low");
  ASSERT_EQ(dev.uart_.tx_count(), size_t(0), "unsupported controls send no frame");
  dev.set_timer_hours(1.5f, false);
  ASSERT_EQ(dev.uart_.tx_frames.back().data[15], 0x86, "pending OFF timer uses quarter hours");
  dev.cmd_humidity(55);
  ASSERT_EQ(dev.uart_.tx_frames.back().data[15], 0x86, "ordinary writes preserve the timer");
  dev.set_timer_hours(0, false);
  ASSERT_EQ(dev.uart_.tx_frames.back().data[15], 0x7F, "cancel restores captured timer sentinel");
  struct Button : esphome::midea_dehum::MideaFilterCleanedButton {
    using MideaFilterCleanedButton::press_action;
  } filter;
  dev.set_filter_cleaned_button(&filter);
  filter.press_action();
  ASSERT_EQ(dev.uart_.tx_frames.back().data[19], 0x80, "filter acknowledgement is explicit");
  dev.sendSetStatus();
  ASSERT_EQ(dev.uart_.tx_frames.back().data[19], 0, "unrelated control clears acknowledgement");
  dev.set_beep_state(true);
  dev.set_beep_state(false);
  ASSERT_EQ(dev.uart_.tx_frames.back().data[11] & 0x40, 0, "command beep can be silenced");
}

static void test_v3_poll_first() {
  for (uint8_t version : {0, 3}) {
    reset_scheduler();
    TestMideaDehum dev;
    dev.set_protocol_version(version);
    dev.setup();
    // Stale HA controls during startup must not replace the peer's mode/fan.
    esphome::climate::ClimateCall call;
    call.preset_ = "ClothesDrying";
    call.fan_mode_ = esphome::climate::CLIMATE_FAN_MEDIUM;
    call.target_humidity_ = 45;
    dev.control(call);
    uint8_t poll[sizeof(V3_FACTORY_TARGET_60_RESPONSE)];
    memcpy(poll, V3_FACTORY_TARGET_60_RESPONSE, sizeof(poll));
    poll[9] = 0x03;  // same captured body, header sum changes by one
    --poll[34];
    tx_clear(dev);
    dev.rx_enqueue(poll, sizeof(poll));
    dev.loop();
    ASSERT(dev.is_v3_active(), "polled C8 identifies V3 without a seed or ACK");
    size_t writes = 0;
    for (const auto& tx : dev.uart_.tx_frames) {
      if (tx.data.size() != 34 || tx.data[10] != 0x48) continue;
      ++writes;
      ASSERT_EQ(tx.data[12], poll[12], "unsupported startup preset preserves reported mode");
      ASSERT_EQ(tx.data[13], poll[13] | 0x80, "unsupported startup fan preserves reported speed");
      ASSERT_EQ(tx.data[17], 45, "valid startup humidity survives discovery");
    }
    ASSERT_EQ(writes, size_t(1), "first poll flushes one control");
  }
}

static int run_v3_tests() {
  int total = run_test("V3 discovery from ACK or status", test_v3_discovery);
  total += run_test("V3 frame discrimination", test_v3_discrimination);
  total += run_test("V3 handshake and deferred control", test_v3_startup);
  total += run_test("V3 factory commands and polls", test_v3_captured_transactions);
  total += run_test("V3 watchdog and legacy fallback", test_v3_recovery);
  total += run_test("V3 optional controls", test_v3_optional_controls);
  total += run_test("V3 poll-first startup", test_v3_poll_first);
  return total;
}
