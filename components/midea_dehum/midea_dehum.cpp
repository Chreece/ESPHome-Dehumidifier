#include "midea_dehum.h"
#include "esphome/core/log.h"
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

// ===== Global protocol buffers ==============================================
static byte networkStatus[20];
static byte currentHeader[10];
static byte getStatusCommand[21] = {
  0x41, 0x81, 0x00, 0xff, 0x03, 0xff,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03
};
static byte setStatusCommand[25];
static byte serialRxBuf[256];
static byte serialTxBuf[256];

static dehumidifierState_t state = {false, smart, medium, 50, 0, 0};

// ===== Helper CRC/checksum ===================================================
static byte crc8(const byte *data, size_t len) {
  byte crc = 0;
  while (len--) {
    byte extract = *data++;
    for (byte i = 8; i; i--) {
      byte sum = (crc ^ extract) & 0x01;
      crc >>= 1;
      if (sum) crc ^= 0x8C;
      extract >>= 1;
    }
  }
  return crc;
}

static byte checksum(const byte *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  return static_cast<byte>(sum & 0xFF);
}

// ===== Fan/Mode conversion helpers ==========================================
static climate::ClimateFanMode fan_to_esphome(fanSpeed_t f) {
  switch (f) {
    case low:    return climate::CLIMATE_FAN_LOW;
    case high:   return climate::CLIMATE_FAN_HIGH;
    case medium:
    default:     return climate::CLIMATE_FAN_MEDIUM;
  }
}
static fanSpeed_t esphome_to_fan(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW:    return low;
    case climate::CLIMATE_FAN_HIGH:   return high;
    case climate::CLIMATE_FAN_MEDIUM:
    default:                          return medium;
  }
}
static std::string mode_to_preset_string(dehumMode_t m) {
  switch (m) {
    case setpoint:      return "setpoint";
    case continuous:    return "continuous";
    case clothesDrying: return "clothesDrying";
    case smart:
    default:            return "smart";
  }
}
static dehumMode_t preset_string_to_mode(const std::string &s) {
  if (s == "setpoint")      return setpoint;
  if (s == "continuous")    return continuous;
  if (s == "clothesDrying") return clothesDrying;
  return smart;
}

// ===== Component lifecycle ==================================================
MideaDehumComponent::~MideaDehumComponent() = default;

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent *uart) {
  this->set_uart_parent(uart);
  this->uart_ = uart;
  ESP_LOGI(TAG, "UART parent set and pointer stored.");
}

void MideaDehumComponent::setup() {
  this->mode = state.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;
  this->fan_mode = fan_to_esphome(state.fanSpeed);
  this->target_temperature = state.humiditySetpoint;
  this->current_temperature = state.currentHumidity;
  this->custom_preset = mode_to_preset_string(state.mode);
  this->publishState();
  if (this->error_sensor_) this->error_sensor_->publish_state(state.errorCode);
  this->updateAndSendNetworkStatus(true);
}

void MideaDehumComponent::loop() {
  static uint32_t last_request = 0;
  uint32_t now = millis();

  if (now - last_request > 3000) {
    last_request = now;
    this->getStatus();
  }

  this->handleUart();
  delay(1);
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_temperature(true);
  t.set_visual_min_temperature(30.0f);
  t.set_visual_max_temperature(80.0f);
  t.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_DRY});
  t.set_supported_fan_modes({
    climate::CLIMATE_FAN_LOW,
    climate::CLIMATE_FAN_MEDIUM,
    climate::CLIMATE_FAN_HIGH
  });
  t.set_supported_custom_presets(std::set<std::string>{
    "smart", "setpoint", "continuous", "clothesDrying"
  });
  return t;
}

// ===== Protocol-named functions =============================================
void MideaDehumComponent::parseState() {
  state.powerOn = (serialRxBuf[11] & 0x01) > 0;
  state.mode = static_cast<dehumMode_t>(serialRxBuf[12] & 0x0f);
  state.fanSpeed = static_cast<fanSpeed_t>(serialRxBuf[13] & 0x7f);
  state.humiditySetpoint = (serialRxBuf[17] >= 100) ? 99 : serialRxBuf[17];
  state.currentHumidity = serialRxBuf[26];
  state.errorCode = serialRxBuf[31];
  this->clearRxBuf();
}

void MideaDehumComponent::clearRxBuf() { memset(serialRxBuf, 0, sizeof(serialRxBuf)); }
void MideaDehumComponent::clearTxBuf() { memset(serialTxBuf, 0, sizeof(serialTxBuf)); }

void MideaDehumComponent::handleUart() {
  if (!uart_) return;
  size_t idx = 0;
  while (uart_->available() && idx < sizeof(serialRxBuf)) {
    uint8_t b;
    if (uart_->read_byte(&b)) serialRxBuf[idx++] = b;
    else break;
  }
  if (idx == 0) return;

  if (idx > 11 && serialRxBuf[10] == 0xC8) {
    this->parseState();
    this->publishState();
  } else if (idx > 11 && serialRxBuf[10] == 0x63) {
    this->updateAndSendNetworkStatus(true);
    this->clearRxBuf();
  } else if (
    idx > 66 &&
    serialRxBuf[10] == 0x00 &&
    serialRxBuf[50] == 0xaa &&
    serialRxBuf[51] == 0x1e &&
    serialRxBuf[52] == 0xa1 &&
    serialRxBuf[58] == 0x03 &&
    serialRxBuf[59] == 0x64 &&
    serialRxBuf[61] == 0x01 &&
    serialRxBuf[65] == 0x01
  ) {
    this->resetWifiSettingsAndReboot();
    this->clearRxBuf();
  } else {
    if (idx == sizeof(serialRxBuf)) this->clearRxBuf();
  }
}

void MideaDehumComponent::writeHeader(byte msgType, byte agreementVersion, byte packetLength) {
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

void MideaDehumComponent::handleStateUpdateRequest(String requestedState, String mode, String fanSpeed, byte humiditySetpoint) {
  dehumidifierState_t newState = state;

  if (requestedState == "on") newState.powerOn = true;
  else if (requestedState == "off") newState.powerOn = false;

  if (mode == "setpoint") newState.mode = setpoint;
  else if (mode == "continuous") newState.mode = continuous;
  else if (mode == "smart") newState.mode = smart;
  else if (mode == "clothesDrying") newState.mode = clothesDrying;

  if (fanSpeed == "low") newState.fanSpeed = low;
  else if (fanSpeed == "medium") newState.fanSpeed = medium;
  else if (fanSpeed == "high") newState.fanSpeed = high;

  if (humiditySetpoint && humiditySetpoint >= 35 && humiditySetpoint <= 85)
    newState.humiditySetpoint = humiditySetpoint;

  if (newState.powerOn != state.powerOn ||
      newState.mode != state.mode ||
      newState.fanSpeed != state.fanSpeed ||
      newState.humiditySetpoint != state.humiditySetpoint) {
    this->updateSetStatus(newState.powerOn, newState.mode, newState.fanSpeed, newState.humiditySetpoint);
    this->sendSetStatus();
    state = newState;
    delay(30);
  }
}

void MideaDehumComponent::sendSetStatus() {
  this->sendMessage(0x02, 0x03, 25, setStatusCommand);
}

void MideaDehumComponent::updateSetStatus(boolean powerOn, dehumMode_t dehumMode, fanSpeed_t fanSpeed, byte humiditySetpoint) {
  memset(setStatusCommand, 0, sizeof(setStatusCommand));
  setStatusCommand[0] = 0x48;
  setStatusCommand[1] = powerOn ? 0x01 : 0x00;
  setStatusCommand[2] = (byte)(dehumMode & 0x0f);
  setStatusCommand[3] = (byte)fanSpeed;
  setStatusCommand[7] = humiditySetpoint;
}

void MideaDehumComponent::updateAndSendNetworkStatus(boolean isConnected) {
  this->updateNetworkStatus(isConnected);
  this->sendMessage(0x0D, 0x03, 20, networkStatus);
}

void MideaDehumComponent::getStatus() {
  this->sendMessage(0x03, 0x03, 21, getStatusCommand);
}

void MideaDehumComponent::updateNetworkStatus(boolean isConnected) {
  memset(networkStatus, 0, sizeof(networkStatus));
  networkStatus[0] = 0x01;
  networkStatus[1] = 0x01;
  networkStatus[2] = 0x04;
  networkStatus[3] = 1;
  networkStatus[4] = 0;
  networkStatus[5] = 0;
  networkStatus[6] = 127;
  networkStatus[7] = 0xFF;
  networkStatus[8] = isConnected ? 0x00 : 0x01;
  networkStatus[9] = isConnected ? 0x00 : 0x01;
  networkStatus[10] = 0x00;
  networkStatus[11] = 0x00;
}

void MideaDehumComponent::sendMessage(byte msgType, byte agreementVersion, byte payloadLength, byte *payload) {
  this->clearTxBuf();
  this->writeHeader(msgType, agreementVersion, payloadLength);
  memcpy(serialTxBuf, currentHeader, 10);
  memcpy(serialTxBuf + 10, payload, payloadLength);
  serialTxBuf[10 + payloadLength]     = crc8(serialTxBuf + 10, payloadLength);
  serialTxBuf[10 + payloadLength + 1] = checksum(serialTxBuf, 10 + payloadLength + 1);
  this->write_array(serialTxBuf, 10 + payloadLength + 2);
  this->flush();
}

// ===== ESPHome Bridge Functions ============================================
void MideaDehumComponent::publishState() {
  this->mode = state.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;
  this->fan_mode = fan_to_esphome(state.fanSpeed);
  this->custom_preset = mode_to_preset_string(state.mode);
  this->target_temperature = state.humiditySetpoint;
  this->current_temperature = state.currentHumidity;
  if (this->error_sensor_) this->error_sensor_->publish_state(state.errorCode);
  this->publish_state();
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  String requestedState = state.powerOn ? "on" : "off";
  String reqMode = mode_to_preset_string(state.mode).c_str();
  String reqFan = "medium";
  byte reqSet = state.humiditySetpoint;

  if (call.get_mode().has_value())
    requestedState = *call.get_mode() == climate::CLIMATE_MODE_OFF ? "off" : "on";

  if (call.get_custom_preset().has_value())
    reqMode = call.get_custom_preset()->c_str();

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW: reqFan = "low"; break;
      case climate::CLIMATE_FAN_HIGH: reqFan = "high"; break;
      case climate::CLIMATE_FAN_MEDIUM:
      default: reqFan = "medium"; break;
    }
  }

  if (call.get_target_temperature().has_value()) {
    float t = *call.get_target_temperature();
    if (t >= 35.0f && t <= 85.0f) reqSet = (byte)t;
  }

  this->handleStateUpdateRequest(requestedState, reqMode, reqFan, reqSet);
  this->publishState();
}

}  // namespace midea_dehum
}  // namespace esphome
