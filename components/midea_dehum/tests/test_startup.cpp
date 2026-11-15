#include "fixtures.h"

// Inspect transmitted commands, not just optimistic Home Assistant state.
static std::vector<std::vector<uint8_t>> startup_commands(const TestMideaDehum& dev) {
  std::vector<std::vector<uint8_t>> result;
  for (const auto& frame : dev.uart_.tx_frames)
    if (frame.data.size() > 17 && frame.data[10] == 0x48) result.push_back(frame.data);
  return result;
}

static void test_startup_power_intent() {
  for (uint8_t version : {1, 2}) {
    for (bool desired : {false, true}) {
      TestMideaDehum dev;
      dev.set_protocol_version(version);
      dev.setup();
      esphome::climate::ClimateCall call;
      call.mode_ = desired ? esphome::climate::CLIMATE_MODE_DRY : esphome::climate::CLIMATE_MODE_OFF;
      dev.control(call);
      ASSERT(startup_commands(dev).empty(), "no command before first real status");
      const uint8_t* status = version == 1 ? (desired ? V1_STATUS : V1_STATUS_ON)
                                          : (desired ? V2_STATUS : V2_STATUS_ON);
      dev.inject(status, sizeof(V1_STATUS));
      auto commands = startup_commands(dev);
      ASSERT_EQ(commands.size(), size_t(1), "power request flushed exactly once");
      if (!commands.empty()) {
        ASSERT_EQ(commands.back()[11] & 1, desired, "requested power survives first status");
        ASSERT_EQ(commands.back()[12], status[12], "unrequested mode comes from device");
        ASSERT_EQ(commands.back()[17], status[17], "unrequested humidity comes from device");
      }
      dev.inject(status, sizeof(V1_STATUS));
      ASSERT_EQ(startup_commands(dev).size(), size_t(1), "later status does not replay startup request");
    }
  }
}

static void test_startup_humidity_intent() {
  for (uint8_t version : {1, 2}) {
    for (uint8_t desired : {50, 65}) {
      TestMideaDehum dev;
      dev.set_protocol_version(version);
      dev.setup();
      esphome::climate::ClimateCall call;
      call.target_humidity_ = desired;  // no implicit power request
      dev.control(call);
      ASSERT(startup_commands(dev).empty(), "humidity waits for known state");
      dev.inject(version == 1 ? V1_STATUS_ON : V2_STATUS_ON, sizeof(V1_STATUS_ON));
      auto commands = startup_commands(dev);
      ASSERT_EQ(commands.size(), size_t(1), "humidity request flushed once");
      if (!commands.empty()) {
        ASSERT_EQ(commands.back()[17], desired, "requested humidity survives, even if it matches defaults");
        ASSERT_EQ(commands.back()[11] & 1, 1, "humidity request preserves device power ON");
        ASSERT_EQ(commands.back()[12], 1, "humidity request preserves device mode");
        ASSERT_EQ(commands.back()[13] & 0x7F, version == 1 ? 40 : 0x28,
                  "humidity request preserves device Low fan");
      }
    }
  }
}

static void test_startup_fan_intent() {
  for (uint8_t version : {1, 2}) {
    TestMideaDehum dev;
    dev.set_protocol_version(version);
    dev.setup();
    esphome::climate::ClimateCall call;
    call.fan_mode_ = esphome::climate::CLIMATE_FAN_HIGH;
    dev.control(call);
    ASSERT(startup_commands(dev).empty(), "fan waits for known state");
    dev.inject(version == 1 ? V1_STATUS_ON : V2_STATUS_ON, sizeof(V1_STATUS_ON));
    auto commands = startup_commands(dev);
    ASSERT_EQ(commands.size(), size_t(1), "fan request flushed once");
    if (!commands.empty()) {
      ASSERT_EQ(commands.back()[13], version == 1 ? 80 : 0xD0, "requested High fan survives first Low status");
      ASSERT_EQ(commands.back()[11] & 1, 1, "fan request preserves device power ON");
      ASSERT_EQ(commands.back()[12], 1, "fan request preserves device mode");
      ASSERT_EQ(commands.back()[17], 60, "fan request preserves device humidity");
    }
  }
}

static void test_startup_merged_intent() {
  for (uint8_t version : {1, 2}) {
    TestMideaDehum dev;
    dev.set_protocol_version(version);
    dev.setup();
    esphome::climate::ClimateCall first;
    first.mode_ = esphome::climate::CLIMATE_MODE_OFF;
    first.preset_ = "Continuous";
    first.target_humidity_ = 75;
    first.fan_mode_ = esphome::climate::CLIMATE_FAN_HIGH;
    dev.control(first);
    esphome::climate::ClimateCall last;
    last.mode_ = esphome::climate::CLIMATE_MODE_DRY;
    last.target_humidity_ = 65;
    dev.control(last);
    ASSERT(startup_commands(dev).empty(), "multiple requests remain deferred");
    dev.inject(version == 1 ? V1_STATUS : V2_STATUS, sizeof(V1_STATUS));
    auto commands = startup_commands(dev);
    ASSERT_EQ(commands.size(), size_t(1), "coalesced startup requests produce one command");
    if (!commands.empty()) {
      ASSERT_EQ(commands.back()[11] & 1, 1, "latest power request wins");
      ASSERT_EQ(commands.back()[12], 2, "earlier requested mode retained");
      ASSERT_EQ(commands.back()[13], version == 1 ? 80 : 0xD0, "earlier requested fan retained");
      ASSERT_EQ(commands.back()[17], 65, "latest humidity request wins");
    }
  }
}

static void test_startup_feature_intent() {
  TestMideaDehum dev;
  dev.setup();
  dev.set_ion_state(true);
  dev.set_sleep_state(true);
  dev.set_pump_state(false);  // explicitly OFF, even though default is also OFF
  esphome::climate::ClimateCall call;
  call.swing_mode_ = esphome::climate::CLIMATE_SWING_VERTICAL;
  dev.control(call);
  std::vector<uint8_t> status(V1_STATUS_ON, V1_STATUS_ON + sizeof(V1_STATUS_ON));
  status[19] = 0x08;  // device pump ON, ion/sleep OFF
  dev.inject(status.data(), status.size());
  auto commands = startup_commands(dev);
  ASSERT_EQ(commands.size(), size_t(1), "feature requests coalesce into one command");
  if (!commands.empty()) {
    ASSERT_EQ(commands.back()[19] & 0x78, 0x70, "requested ion/sleep/pump survive device status");
    ASSERT_EQ(commands.back()[20] & 0x08, 0x08, "requested vertical swing survives device status");
    ASSERT_EQ(commands.back()[17], 60, "feature changes preserve unrequested setpoint");
  }
}

static void test_startup_timer_and_reset() {
  for (bool reset_first : {false, true}) {
    TestMideaDehum dev;
    dev.set_protocol_version(2);
    dev.setup();
    if (reset_first) dev.sendResetWaterLevel();
    dev.set_timer_hours(3, false);
    dev.cmd_power(true);
    if (!reset_first) dev.sendResetWaterLevel();
    ASSERT(dev.uart_.tx_frames.empty(), "neither settings nor reset sent before known state");
    dev.inject(V2_STATUS, sizeof(V2_STATUS));
    auto commands = startup_commands(dev);
    ASSERT_EQ(commands.size(), size_t(1), "reset does not discard pending settings");
    if (!commands.empty()) {
      ASSERT_EQ(commands.back()[14], 0, "timer does not target default OFF state's ON timer");
      ASSERT_EQ(commands.back()[15], 0x8C, "timer targets merged ON state's OFF timer");
    }
    ASSERT_EQ(dev.uart_.tx_count(), size_t(2), "settings and reset both sent once");
    if (dev.uart_.tx_count() == 2) {
      ASSERT_EQ(dev.uart_.tx_at(1).data[10], 0xC8, "reset follows merged set-status");
      ASSERT_EQ(dev.uart_.tx_at(1).data[11] & 1, 1, "reset uses merged power state");
    }
  }
}

static int run_startup_tests() {
  int total = 0;
  total += run_test("ST.1 Deferred power", test_startup_power_intent);
  total += run_test("ST.2 Deferred humidity", test_startup_humidity_intent);
  total += run_test("ST.3 Deferred fan", test_startup_fan_intent);
  total += run_test("ST.4 Coalesced startup requests", test_startup_merged_intent);
  total += run_test("ST.5 Deferred feature values", test_startup_feature_intent);
  total += run_test("ST.6 Deferred timer and reset", test_startup_timer_and_reset);
  return total;
}

#ifndef TEST_COMBINED
int main() { return run_startup_tests() > 0 ? 1 : 0; }
#endif
