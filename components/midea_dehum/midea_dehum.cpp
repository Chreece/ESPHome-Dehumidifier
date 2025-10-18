#include "midea_dehum.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"
#include <cmath>
#ifdef USE_MIDEA_DEHUM_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_MIDEA_DEHUM_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_MIDEA_DEHUM_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

static uint8_t networkStatus[20];
static uint8_t currentHeader[10];
static uint8_t getStatusCommand[21] = {
  0x41, 0x81, 0x00, 0xff, 0x03, 0xff,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03
};

static uint8_t setStatusCommand[25];
static uint8_t serialRxBuf[256];
static uint8_t serialTxBuf[256];

static const uint8_t crc_table[] = {
  0x00,0x5e,0xbc,0xE2,0x61,0x3F,0xDD,0x83,
  0xC2,0x9C,0x7E,0x20,0xA3,0xFD,0x1F,0x41,
  0x9D,0xC3,0x21,0x7F,0xFC,0xA2,0x40,0x1E,
  0x5F,0x01,0xE3,0xBD,0x3E,0x60,0x82,0xDC,
  0x23,0x7D,0x9F,0xC1,0x42,0x1C,0xFE,0xA0,
  0xE1,0xBF,0x5D,0x03,0x80,0xDE,0x3C,0x62,
  0xBE,0xE0,0x02,0x5C,0xDF,0x81,0x63,0x3D,
  0x7C,0x22,0xC0,0x9E,0x1D,0x43,0xA1,0xFF,
  0x46,0x18,0xFA,0xA4,0x27,0x79,0x9B,0xC5,
  0x84,0xDA,0x38,0x66,0xE5,0xBB,0x59,0x07,
  0xDB,0x85,0x67,0x39,0xBA,0xE4,0x06,0x58,
  0x19,0x47,0xA5,0xFB,0x78,0x26,0xC4,0x9A,
  0x65,0x3B,0xD9,0x87,0x04,0x5A,0xB8,0xE6,
  0xA7,0xF9,0x1B,0x45,0xC6,0x98,0x7A,0x24,
  0xF8,0xA6,0x44,0x1A,0x99,0xC7,0x25,0x7B,
  0x3A,0x64,0x86,0xD8,0x5B,0x05,0xE7,0xB9,
  0x8C,0xD2,0x30,0x6E,0xED,0xB3,0x51,0x0F,
  0x4E,0x10,0xF2,0xAC,0x2F,0x71,0x93,0xCD,
  0x11,0x4F,0xAD,0xF3,0x70,0x2E,0xCC,0x92,
  0xD3,0x8D,0x6F,0x31,0xB2,0xEC,0x0E,0x50,
  0xAF,0xF1,0x13,0x4D,0xCE,0x90,0x72,0x2C,
  0x6D,0x33,0xD1,0x8F,0x0C,0x52,0xB0,0xEE,
  0x32,0x6C,0x8E,0xD0,0x53,0x0D,0xEF,0xB1,
  0xF0,0xAE,0x4C,0x12,0x91,0xCF,0x2D,0x73,
  0xCA,0x94,0x76,0x28,0xAB,0xF5,0x17,0x49,
  0x08,0x56,0xB4,0xEA,0x69,0x37,0xD5,0x8B,
  0x57,0x09,0xEB,0xB5,0x36,0x68,0x8A,0xD4,
  0x95,0xCB,0x29,0x77,0xF4,0xAA,0x48,0x16,
  0xE9,0xB7,0x55,0x0B,0x88,0xD6,0x34,0x6A,
  0x2B,0x75,0x97,0xC9,0x4A,0x14,0xF6,0xA8,
  0x74,0x2A,0xC8,0x96,0x15,0x4B,0xA9,0xF7,
  0xB6,0xE8,0x0A,0x54,0xD7,0x89,0x6B,0x35
};

struct dehumidifierState_t {
  bool powerOn;
  uint8_t mode;
  uint8_t fanSpeed;
  uint8_t humiditySetpoint;
  uint8_t currentHumidity;
  uint8_t currentTemperature;
  uint8_t errorCode;
};
static dehumidifierState_t state = {false, 3, 60, 50, 0, 0, 0};

static uint8_t crc8(uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;
  while (len--) crc = crc_table[*addr++ ^ crc];
  return crc;
}

static uint8_t checksum(uint8_t *addr, uint8_t len) {
  uint8_t sum = 0;
  addr++;  // skip 0xAA
  while (len--) sum = sum + *addr++;
  return 256 - sum;
}

#ifdef USE_MIDEA_DEHUM_ERROR
void MideaDehumComponent::set_error_sensor(sensor::Sensor *s) {
  this->error_sensor_ = s;
}
#endif

#ifdef USE_MIDEA_DEHUM_BUCKET
void MideaDehumComponent::set_bucket_full_sensor(binary_sensor::BinarySensor *s) { this->bucket_full_sensor_ = s; }
#endif

#ifdef USE_MIDEA_DEHUM_ION
void MideaDehumComponent::set_ion_state(bool on) {
  if (this->ion_state_ == on) return;
  this->ion_state_ = on;
  ESP_LOGI(TAG, "Ionizer %s", on ? "ON" : "OFF");
  this->sendSetStatus();
}
void MideaDehumComponent::set_ion_switch(MideaIonSwitch *s) {
  this->ion_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaIonSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_ion_state(state);
}
#endif
#ifdef USE_MIDEA_DEHUM_SWING
void MideaDehumComponent::set_swing_state(bool on) {
  if (this->swing_state_ == on) return;
  this->swing_state_ = on;
  ESP_LOGI(TAG, "Swing %s", on ? "ON" : "OFF");
  this->sendSetStatus();
}

void MideaDehumComponent::set_swing_switch(MideaSwingSwitch *s) {
  this->swing_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaSwingSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_swing_state(state);
}
#endif
#ifdef USE_MIDEA_DEHUM_BEEP

void MideaDehumComponent::set_beep_switch(MideaBeepSwitch *s) {
  this->beep_switch_ = s;
  if (s) s->set_parent(this);                     // << CRUCIAL
  if (this->beep_switch_)
    this->beep_switch_->publish_state(this->beep_state_);
}

void MideaDehumComponent::set_beep_state(bool on) {
  bool was = this->beep_state_;
  this->beep_state_ = on;

  ESP_LOGI(TAG, "Beeper request: %s (was %s)",
           on ? "ON" : "OFF", was ? "ON" : "OFF");

  // Persist
  auto pref = global_preferences->make_preference<bool>(0xBEE1234);
  bool saved = this->beep_state_;
  pref.save(&saved);

  // Apply immediately (ALWAYS send, even if 'on' didn't change)
  this->sendSetStatus();

  // Keep HA in sync
  if (this->beep_switch_)
    this->beep_switch_->publish_state(this->beep_state_);
}

void MideaDehumComponent::restore_beep_state() {
  auto pref = global_preferences->make_preference<bool>(0xBEE1234);
  bool saved_state = false;
  if (pref.load(&saved_state)) {
    this->beep_state_ = saved_state;
    ESP_LOGI(TAG, "Restored Beeper state: %s", saved_state ? "ON" : "OFF");
  } else {
    this->beep_state_ = false;
    ESP_LOGI(TAG, "No saved Beeper state found. Defaulting to OFF.");
  }
  if (this->beep_switch_) this->beep_switch_->publish_state(this->beep_state_);
}

void MideaBeepSwitch::write_state(bool state) {
  ESP_LOGI("midea_dehum", "Beep switch write_state(%s)", state ? "ON" : "OFF");
  if (!this->parent_) return;
  this->parent_->set_beep_state(state);
  this->publish_state(state);
}
#endif

#ifdef USE_MIDEA_DEHUM_SLEEP
void MideaDehumComponent::set_sleep_switch(MideaSleepSwitch *s) {
  this->sleep_switch_ = s;
  if (s) s->set_parent(this);
  if (this->sleep_switch_)
    this->sleep_switch_->publish_state(this->sleep_state_);
}

void MideaDehumComponent::set_sleep_state(bool on) {
  this->sleep_state_ = on;
  auto pref = global_preferences->make_preference<bool>(0x5E33123);
  pref.save(&this->sleep_state_);
  this->sendSetStatus();
  if (this->sleep_switch_) this->sleep_switch_->publish_state(this->sleep_state_);
}

void MideaDehumComponent::restore_sleep_state() {
  auto pref = global_preferences->make_preference<bool>(0x5E33123);
  bool saved = false;
  if (pref.load(&saved)) {
    this->sleep_state_ = saved;
    ESP_LOGI(TAG, "Restored sleep mode: %s", saved ? "ON" : "OFF");
  } else {
    this->sleep_state_ = false;
  }
  if (this->sleep_switch_) this->sleep_switch_->publish_state(this->sleep_state_);
}

void MideaSleepSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_sleep_state(state);
  this->publish_state(state);
}
#endif

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent *uart) {
  this->set_uart_parent(uart);
  this->uart_ = uart;
  ESP_LOGI(TAG, "UART parent set and pointer stored.");
}

void MideaDehumComponent::setup() {
#ifdef USE_MIDEA_DEHUM_BEEP
  this->restore_beep_state();
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  this->restore_sleep_state();
#endif
  App.scheduler.set_timeout(this, "initial_network", 3000, [this]() {
    this->updateAndSendNetworkStatus();
  });

  App.scheduler.set_timeout(this, "init_get_status", 5000, [this]() {
    this->getStatus();
  });
}

void MideaDehumComponent::loop() {
  this->handleUart();

  static uint32_t last_status_poll = 0;
  const uint32_t status_poll_interval = 3000;
  uint32_t now = millis();
  if (now - last_status_poll >= this->status_poll_interval_) {
    last_status_poll = now;
    this->getStatus();
  }
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_temperature(true);
  t.set_supports_current_humidity(true);
  t.set_supports_target_humidity(true);
  t.set_visual_min_humidity(30.0f);
  t.set_visual_max_humidity(80.0f);
  t.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_DRY});
  t.set_supported_fan_modes({
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH
  });
  std::set<std::string> custom_presets;
  if (display_mode_setpoint_ != "UNUSED") custom_presets.insert(display_mode_setpoint_);
  if (display_mode_continuous_ != "UNUSED") custom_presets.insert(display_mode_continuous_);
  if (display_mode_smart_ != "UNUSED") custom_presets.insert(display_mode_smart_);
  if (display_mode_clothes_drying_ != "UNUSED") custom_presets.insert(display_mode_clothes_drying_);

  if (!custom_presets.empty()) {
    t.set_supported_custom_presets(custom_presets);
  }
  return t;
}

void MideaDehumComponent::parseState() {
  // --- Basic operating parameters ---
  state.powerOn          = (serialRxBuf[11] & 0x01) != 0;
  state.mode              = serialRxBuf[12] & 0x0F;
  state.fanSpeed          = serialRxBuf[13] & 0x7F;
  state.humiditySetpoint  = (serialRxBuf[17] > 100) ? 99 : serialRxBuf[17];

  // --- Panel light / brightness class (bits 7–6) ---
#ifdef USE_MIDEA_DEHUM_LIGHT
  uint8_t new_light_class = (serialRxBuf[19] & 0xC0) >> 6;
  if (new_light_class != this->light_class_) {
    this->light_class_ = new_light_class;
    if (this->light_select_) {
      const char* light_str =
        new_light_class == 0 ? "Auto" :
        new_light_class == 1 ? "Off" :
        new_light_class == 2 ? "Low" : "High";
      this->light_select_->publish_state(light_str);
    }
  }
#endif

  // --- Ionizer (bit 6) ---
#ifdef USE_MIDEA_DEHUM_ION
  bool new_ion_state = (serialRxBuf[19] & 0x40) != 0;
  if (new_ion_state != this->ion_state_) {
    this->ion_state_ = new_ion_state;
    if (this->ion_switch_) this->ion_switch_->publish_state(new_ion_state);
  }
#endif

  // --- Sleep mode (bit 5) ---
#ifdef USE_MIDEA_DEHUM_SLEEP
  bool new_sleep_state = (serialRxBuf[19] & 0x20) != 0;
  if (new_sleep_state != this->sleep_state_) {
    this->sleep_state_ = new_sleep_state;
    if (this->sleep_switch_) this->sleep_switch_->publish_state(new_sleep_state);
  }
#endif

  // --- Optional: Pump bits (3–4) ---
#ifdef USE_MIDEA_DEHUM_PUMP
  bool new_pump_state = (serialRxBuf[19] & 0x08) != 0;
  bool new_pump_flag  = (serialRxBuf[19] & 0x10) != 0;
  if (new_pump_state != this->pump_state_) {
    this->pump_state_ = new_pump_state;
    if (this->pump_switch_) this->pump_switch_->publish_state(new_pump_state);
  }
  this->pump_flag_ = new_pump_flag;
#endif

  // --- Vertical swing (byte 20, bit 5) ---
#ifdef USE_MIDEA_DEHUM_SWING
  bool new_swing_state = (serialRxBuf[20] & 0x20) != 0;
  if (new_swing_state != this->swing_state_) {
    this->swing_state_ = new_swing_state;
    if (this->swing_switch_) this->swing_switch_->publish_state(new_swing_state);
  }
#endif

  // --- Environmental readings ---
  state.currentHumidity = serialRxBuf[26];
  state.currentTemperature = (static_cast<int>(serialRxBuf[27]) - 50) / 2;
  state.errorCode = serialRxBuf[31];

  ESP_LOGI(TAG,
    "Parsed -> Power:%s Mode:%u Fan:%u Target:%u CurrentH:%u Temp:%d Err:%u",
    state.powerOn ? "ON" : "OFF",
    state.mode, state.fanSpeed,
    state.humiditySetpoint, state.currentHumidity,
    state.currentTemperature, state.errorCode
  );

  this->clearRxBuf();
}

void MideaDehumComponent::clearRxBuf() { memset(serialRxBuf, 0, sizeof(serialRxBuf)); }
void MideaDehumComponent::clearTxBuf() { memset(serialTxBuf, 0, sizeof(serialTxBuf)); }

void MideaDehumComponent::handleUart() {
  if (!this->uart_) return;

  static size_t rx_len = 0;

  while (this->uart_->available()) {
    uint8_t uint8_t_in;
    if (!this->uart_->read_byte(&uint8_t_in)) break;

    if (rx_len < sizeof(serialRxBuf)) serialRxBuf[rx_len++] = uint8_t_in;
    else rx_len = 0;

    if (rx_len == 1 && serialRxBuf[0] != 0xAA) { rx_len = 0; continue; }

    if (rx_len >= 2) {
      const uint8_t expected_len = serialRxBuf[1];
      if (rx_len >= expected_len) {
        std::string hex_str;
        hex_str.reserve(expected_len * 3);
        for (size_t i = 0; i < rx_len; i++) {
          char buf[4];
          snprintf(buf, sizeof(buf), "%02X ", serialRxBuf[i]);
          hex_str += buf;
        }
        ESP_LOGI(TAG, "RX packet (%u uint8_ts): %s", (unsigned)rx_len, hex_str.c_str());

        if (serialRxBuf[10] == 0xC8) {
          this->parseState();
          this->publishState();
        } else if (serialRxBuf[10] == 0x63) {
          this->updateAndSendNetworkStatus();
        } else if (
          serialRxBuf[0] == 0xAA &&
          serialRxBuf[1] == 0x1E &&
          serialRxBuf[2] == 0xA1 &&
          serialRxBuf[9] == 0x64 &&
          serialRxBuf[11] == 0x01 &&
          serialRxBuf[15] == 0x01
        ) {
          App.scheduler.set_timeout(this, "factory_reset", 500, [this]() {
            ESP_LOGW(TAG, "Performing factory reset...");
            global_preferences->reset();

            App.scheduler.set_timeout(this, "reboot_after_reset", 300, []() {
              App.safe_reboot();
            });
          });
        }

        rx_len = 0;
      }
    }
  }
}

void MideaDehumComponent::writeHeader(uint8_t msgType, uint8_t agreementVersion, uint8_t packetLength) {
  currentHeader[0] = 0xAA;
  currentHeader[1] = 10 + packetLength + 1;
  currentHeader[2] = 0xA1;
  currentHeader[3] = 0x00;
  currentHeader[4] = 0x00;
  currentHeader[5] = 0x00;
  currentHeader[6] = 0x00;
  currentHeader[7] = 0x00;
  currentHeader[8] = agreementVersion;
  currentHeader[9] = msgType;
}

void MideaDehumComponent::handleStateUpdateRequest(std::string requestedState, uint8_t mode, uint8_t fanSpeed, uint8_t humiditySetpoint) {
  dehumidifierState_t newState = state;

  if (requestedState == "on") newState.powerOn = true;
  else if (requestedState == "off") newState.powerOn = false;

  if (mode < 1 || mode > 4) mode = 3;
  newState.mode = mode;
  newState.fanSpeed = fanSpeed;

  if (humiditySetpoint && humiditySetpoint >= 35 && humiditySetpoint <= 85)
    newState.humiditySetpoint = humiditySetpoint;

  if (newState.powerOn != state.powerOn ||
      newState.mode != state.mode ||
      newState.fanSpeed != state.fanSpeed ||
      newState.humiditySetpoint != state.humiditySetpoint) {

    state = newState;
    this->sendSetStatus();
  }
}

void MideaDehumComponent::sendSetStatus() {
  memset(setStatusCommand, 0, sizeof(setStatusCommand));

  // --- Command header ---
  setStatusCommand[0] = 0x48;  // Write command marker

  // --- Power and beep (byte 11) ---
  setStatusCommand[11] = state.powerOn ? 0x01 : 0x00;
#ifdef USE_MIDEA_DEHUM_BEEP
  if (this->beep_state_) setStatusCommand[11] |= 0x40;  // bit6 = beep prompt
#endif

  // --- Mode (byte 12) ---
  uint8_t mode = state.mode;
  if (mode < 1 || mode > 4) mode = 3;
  setStatusCommand[12] = mode & 0x0F;

  // --- Fan speed (byte 13) ---
  setStatusCommand[13] = static_cast<uint8_t>(state.fanSpeed) & 0x7F;

  // --- Target humidity (byte 17) ---
  setStatusCommand[17] = state.humiditySetpoint;

  // --- Misc feature flags (byte 19) ---
  uint8_t b19 = 0;

#ifdef USE_MIDEA_DEHUM_LIGHT
  // bits 7–6 = panel light brightness mode (0–3)
  b19 |= (this->light_class_ & 0x03) << 6;
#endif
#ifdef USE_MIDEA_DEHUM_ION
  if (this->ion_state_) b19 |= 0x40;  // bit6 = ionizer on
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  if (this->sleep_state_) b19 |= 0x20;  // bit5 = sleep
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  if (this->pump_state_) b19 |= 0x08;   // bit3 = pump enable
  if (this->pump_flag_)  b19 |= 0x10;   // bit4 = pump disable flag
#endif

  setStatusCommand[19] = b19;

  // --- Swing (byte 20, bit5) ---
#ifdef USE_MIDEA_DEHUM_SWING
  if (this->swing_state_) setStatusCommand[20] |= 0x20;
#endif

  // --- Send assembled frame ---
  this->sendMessage(0x02, 0x03, 25, setStatusCommand);
}

void MideaDehumComponent::updateAndSendNetworkStatus() {
  memset(networkStatus, 0, sizeof(networkStatus));

  // Byte 0: module type (Wi-Fi)
  networkStatus[0] = 0x01;

  // Byte 1: Wi-Fi mode
  networkStatus[1] = 0x01;

  networkStatus[2] = 0x04;

  networkStatus[3] = 1;
  networkStatus[4] = 0;
  networkStatus[5] = 0;
  networkStatus[6] = 127;

  // Byte 7: RF signal (not used)
  networkStatus[7] = 0xFF;

  // Byte 8: router status
  networkStatus[8] = 0x00;

  // Byte 9: cloud
  networkStatus[9] = 0x00;

  // Byte 10: Direct LAN connection (not applicable)
  networkStatus[10] = 0x00;

  // Byte 11: TCP connection count (not used)
  networkStatus[11] = 0x00;

  this->sendMessage(0x0D, 0x03, 20, networkStatus);
}

void MideaDehumComponent::getStatus() {
  this->sendMessage(0x03, 0x03, 21, getStatusCommand);
}

void MideaDehumComponent::sendMessage(uint8_t msgType, uint8_t agreementVersion, uint8_t payloadLength, uint8_t *payload) {
  this->clearTxBuf();
  this->writeHeader(msgType, agreementVersion, payloadLength);
  memcpy(serialTxBuf, currentHeader, 10);
  memcpy(serialTxBuf + 10, payload, payloadLength);
  serialTxBuf[10 + payloadLength]     = crc8(serialTxBuf + 10, payloadLength);
  serialTxBuf[10 + payloadLength + 1] = checksum(serialTxBuf, 10 + payloadLength + 1);

  const size_t total_len = 10 + payloadLength + 2;

  ESP_LOGI(TAG, "TX -> msgType=0x%02X, agreementVersion=0x%02X, payloadLength=%u, total=%u",
           msgType, agreementVersion, payloadLength, (unsigned) total_len);
  std::string tx_hex;
  for (size_t i = 0; i < total_len; i++) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02X ", serialTxBuf[i]);
    tx_hex += buf;
  }
  ESP_LOGI(TAG, "TX uint8_ts: %s", tx_hex.c_str());

  this->write_array(serialTxBuf, total_len);
}

void MideaDehumComponent::publishState() {
  this->mode = state.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;

  if (state.fanSpeed <= 50)
    this->fan_mode = climate::CLIMATE_FAN_LOW;
  else if (state.fanSpeed <= 70)
    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  else
    this->fan_mode = climate::CLIMATE_FAN_HIGH;

  std::string current_mode_str;
  switch (state.mode) {
    case 1: current_mode_str = display_mode_setpoint_; break;
    case 2: current_mode_str = display_mode_continuous_; break;
    case 3: current_mode_str = display_mode_smart_; break;
    case 4: current_mode_str = display_mode_clothes_drying_; break;
    default: current_mode_str = display_mode_smart_; break;
  }

  this->custom_preset = current_mode_str;
  this->target_humidity  = int(state.humiditySetpoint);
  this->current_humidity = int(state.currentHumidity);
  this->current_temperature = state.currentTemperature;
#ifdef USE_MIDEA_DEHUM_ERROR
  if (this->error_sensor_ != nullptr){
    this->error_sensor_->publish_state(state.errorCode);
  }
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  const bool bucket_full = (state.errorCode == 38);
  if (this->bucket_full_sensor_)
    this->bucket_full_sensor_->publish_state(bucket_full);
#endif
#ifdef USE_MIDEA_DEHUM_BEEP
  if (this->beep_switch_)
    this->beep_switch_->publish_state(this->beep_state_);
#endif
  this->publish_state();
}

// ===== Climate control =======================================================
void MideaDehumComponent::control(const climate::ClimateCall &call) {
  std::string requestedState = state.powerOn ? "on" : "off";
  uint8_t reqMode = state.mode;
  uint8_t reqFan = state.fanSpeed;
  uint8_t reqSet = state.humiditySetpoint;

  if (call.get_mode().has_value())
    requestedState = *call.get_mode() == climate::CLIMATE_MODE_OFF ? "off" : "on";

  if (call.get_custom_preset().has_value()) {
    std::string requestedPreset = *call.get_custom_preset();
    if (requestedPreset == display_mode_setpoint_)
      reqMode = 1;
    else if (requestedPreset == display_mode_continuous_)
      reqMode = 2;
    else if (requestedPreset == display_mode_smart_)
      reqMode = 3;
    else if (requestedPreset == display_mode_clothes_drying_)
      reqMode = 4;
    else
      reqMode = 3;
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:    reqFan = 40; break;
      case climate::CLIMATE_FAN_MEDIUM: reqFan = 60; break;
      case climate::CLIMATE_FAN_HIGH:   reqFan = 80; break;
      default:                          reqFan = 60; break;
    }
  }

if (call.get_target_humidity().has_value()) {
  float h = *call.get_target_humidity();
  if (h >= 30.0f && h <= 99.0f)
    reqSet = (uint8_t) std::round(h);
}

  this->handleStateUpdateRequest(requestedState, reqMode, reqFan, reqSet);
}

}  // namespace midea_dehum
}  // namespace esphome
