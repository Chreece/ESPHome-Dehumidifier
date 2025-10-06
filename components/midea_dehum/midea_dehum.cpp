#include "midea_dehum.h"
#include "esphome/core/log.h"

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

static uint8_t mode_string_to_int(const std::string &mode_str) {
  if (mode_str == "setpoint")      return 1;
  if (mode_str == "continuous")    return 2;
  if (mode_str == "smart")         return 3;
  if (mode_str == "clothesDrying") return 4;
  return 0;
}

struct dehumidifierState_t { 
  boolean powerOn;
  std::string mode;
  byte fanSpeed;
  byte humiditySetpoint;
  byte currentHumidity;
  byte errorCode;
};

static byte setStatusCommand[25];
static byte serialRxBuf[256];
static byte serialTxBuf[256];

static dehumidifierState_t state = {false, "smart", 60, 50, 0, 0};

static const byte crc_table[] = {
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

static byte crc8(byte *addr, byte len) {
  byte crc = 0;
  while (len--) {
    crc = crc_table[*addr++ ^ crc];
  }
  return crc;
}

// ===== Checksum (skip first byte, return 256 - sum), exactly like reference ==
static byte checksum(byte *addr, byte len) {
  byte sum = 0;
  addr++;           // skip 0xAA
  while (len--) {
    sum = sum + *addr++;
  }
  return 256 - sum;
}

// ===== Component lifecycle ==================================================
MideaDehumComponent::~MideaDehumComponent() = default;

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent *uart) {
  this->set_uart_parent(uart);
  this->uart_ = uart;
  ESP_LOGI(TAG, "UART parent set and pointer stored.");
}

void MideaDehumComponent::setup() {
  this->updateAndSendNetworkStatus(true);
  
  delay(3000);  
  
}

void MideaDehumComponent::loop() {
  this->handleUart();

  static uint32_t last_status_poll = 0;
  const uint32_t status_poll_interval = 30000;

  uint32_t now = millis();
  if (now - last_status_poll >= status_poll_interval) {
    last_status_poll = now;
    this->getStatus();
  }
  delay(1);
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.set_supports_current_temperature(true);
  t.set_visual_min_temperature(30.0f);
  t.set_visual_max_temperature(80.0f);
  t.set_visual_temperature_step(1.0f);
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
  // Extract from RX packet
  state.powerOn = (serialRxBuf[11] & 0x01) > 0;

  // Convert numeric mode (1–4) to string
  uint8_t raw_mode = serialRxBuf[12] & 0x0F;
  switch (raw_mode) {
    case 1:  state.mode = "setpoint"; break;
    case 2:  state.mode = "continuous"; break;
    case 3:  state.mode = "smart"; break;
    case 4:  state.mode = "clothesDrying"; break;
    default: state.mode = "unknown"; break;
  }

  // Fan speed stays as byte, directly from protocol
  state.fanSpeed = serialRxBuf[13] & 0x7F;

  // Humidity targets
  state.humiditySetpoint = serialRxBuf[17] >= 100 ? 99 : serialRxBuf[17];
  state.currentHumidity = serialRxBuf[26];
  state.errorCode = serialRxBuf[31];

  ESP_LOGI(TAG,
    "Parsed -> Power: %s | Mode: %s | Fan: %u | Target: %u | Current: %u | Err: %u",
    state.powerOn ? "ON" : "OFF",
    state.mode.c_str(), state.fanSpeed,
    state.humiditySetpoint, state.currentHumidity,
    state.errorCode
  );

  this->clearRxBuf();
}

void MideaDehumComponent::clearRxBuf() { memset(serialRxBuf, 0, sizeof(serialRxBuf)); }
void MideaDehumComponent::clearTxBuf() { memset(serialTxBuf, 0, sizeof(serialTxBuf)); }

void MideaDehumComponent::handleUart() {
  if (!this->uart_) return;

  static size_t rx_len = 0;

  while (this->uart_->available()) {
    uint8_t byte_in;
    if (!this->uart_->read_byte(&byte_in)) break;

    // Store byte
    if (rx_len < sizeof(serialRxBuf))
      serialRxBuf[rx_len++] = byte_in;
    else
      rx_len = 0;  // buffer overflow protection

    // Look for start of frame
    if (rx_len == 1 && serialRxBuf[0] != 0xAA) {
      rx_len = 0; // discard junk until we see 0xAA
      continue;
    }

    // Once we have at least 2 bytes, we know total expected length
    if (rx_len >= 2) {
      uint8_t expected_len = serialRxBuf[1];
      if (rx_len >= expected_len) {
        // We have a full frame!
        std::string hex_str;
        for (size_t i = 0; i < rx_len; i++) {
          char buf[4];
          snprintf(buf, sizeof(buf), "%02X ", serialRxBuf[i]);
          hex_str += buf;
        }
        ESP_LOGI(TAG, "RX packet (%u bytes): %s", (unsigned)rx_len, hex_str.c_str());

        // === Process full frame ===
        if (serialRxBuf[10] == 0xC8) {
          this->parseState();
          this->publishState();
        } else if (serialRxBuf[10] == 0x63) {
          this->updateAndSendNetworkStatus(true);
        } else if (
          serialRxBuf[10] == 0x00 &&
          serialRxBuf[50] == 0xAA &&
          serialRxBuf[51] == 0x1E &&
          serialRxBuf[52] == 0xA1 &&
          serialRxBuf[58] == 0x03 &&
          serialRxBuf[59] == 0x64 &&
          serialRxBuf[61] == 0x01 &&
          serialRxBuf[65] == 0x01
        ) {
          ESP_LOGW(TAG, "Reset frame detected! Rebooting...");
          delay(1000);
          ESP.restart();
        }

        // Reset for next frame
        rx_len = 0;
      }
    }
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

void MideaDehumComponent::handleStateUpdateRequest(String requestedState, std::string mode, byte fanSpeed, byte humiditySetpoint) {
  dehumidifierState_t newState = state;

  if (requestedState == "on") newState.powerOn = true;
  else if (requestedState == "off") newState.powerOn = false;
  
  newState.mode = mode;
  newState.fanSpeed = fanSpeed;

  if (humiditySetpoint && humiditySetpoint >= 35 && humiditySetpoint <= 85)
    newState.humiditySetpoint = humiditySetpoint;

  if (newState.powerOn != state.powerOn ||
      newState.mode != state.mode ||
      newState.fanSpeed != state.fanSpeed ||
      newState.humiditySetpoint != state.humiditySetpoint) {

    state.powerOn = newState.powerOn;
    state.mode = mode;
    state.fanSpeed = fanSpeed;
    state.humiditySetpoint = humiditySetpoint;
    this->sendSetStatus();
    delay(30);
  }
}

void MideaDehumComponent::sendSetStatus() {
  memset(setStatusCommand, 0, sizeof(setStatusCommand));
  setStatusCommand[0] = 0x48;
  setStatusCommand[1] = state.powerOn ? 0x01 : 0x00;
  uint8_t code = mode_string_to_int(state.mode);
  setStatusCommand[2] = (byte)((code ? code : (serialRxBuf[12] & 0x0F)) & 0x0F);
  setStatusCommand[3] = (byte)state.fanSpeed;
  setStatusCommand[7] = state.humiditySetpoint;
  this->sendMessage(0x02, 0x03, 25, setStatusCommand);
}

void MideaDehumComponent::updateAndSendNetworkStatus(boolean isConnected) {
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
  this->sendMessage(0x0D, 0x03, 20, networkStatus);
}

void MideaDehumComponent::getStatus() {
  this->sendMessage(0x03, 0x03, 21, getStatusCommand);
}

void MideaDehumComponent::sendMessage(byte msgType, byte agreementVersion, byte payloadLength, byte *payload) {
  this->clearTxBuf();
  this->writeHeader(msgType, agreementVersion, payloadLength);
  memcpy(serialTxBuf, currentHeader, 10);
  memcpy(serialTxBuf + 10, payload, payloadLength);
  serialTxBuf[10 + payloadLength]     = crc8(serialTxBuf + 10, payloadLength);
  serialTxBuf[10 + payloadLength + 1] = checksum(serialTxBuf, 10 + payloadLength + 1);

  size_t total_len = 10 + payloadLength + 2;

  // === 🧭 TX Logging ===
  ESP_LOGI(TAG, "TX -> msgType=0x%02X, agreementVersion=0x%02X, payloadLength=%u, total=%u",
           msgType, agreementVersion, payloadLength, (unsigned) total_len);
  String tx_hex;
  for (size_t i = 0; i < total_len; i++) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02X ", serialTxBuf[i]);
    tx_hex += buf;
  }
  ESP_LOGI(TAG, "TX Bytes: %s", tx_hex.c_str());

  this->write_array(serialTxBuf, total_len);
}

// ===== ESPHome Bridge Functions ============================================
void MideaDehumComponent::publishState() {
  this->mode = state.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;

  if (state.fanSpeed <= 50)
    this->fan_mode = climate::CLIMATE_FAN_LOW;
  else if (state.fanSpeed <= 70)
    this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  else
    this->fan_mode = climate::CLIMATE_FAN_HIGH;

  this->custom_preset = state.mode;

  this->target_temperature = int(state.humiditySetpoint);
  this->current_temperature = int(state.currentHumidity);

  if (this->error_sensor_) this->error_sensor_->publish_state(state.errorCode);
  this->publish_state();
}

void MideaDehumComponent::control(const climate::ClimateCall &call) {
  String requestedState = state.powerOn ? "on" : "off";
  std::string reqMode = (state.mode).c_str();
  byte reqFan = state.fanSpeed;
  byte reqSet = state.humiditySetpoint;

  if (call.get_mode().has_value())
    requestedState = *call.get_mode() == climate::CLIMATE_MODE_OFF ? "off" : "on";

  if (call.get_custom_preset().has_value())
    reqMode = call.get_custom_preset()->c_str();

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW: reqFan = 40; break;
      case climate::CLIMATE_FAN_HIGH: reqFan = 80; break;
      case climate::CLIMATE_FAN_MEDIUM: reqFan = 60; break;
      default: reqFan = 60; break;
    }
  }

  if (call.get_target_temperature().has_value()) {
    float t = *call.get_target_temperature();
    if (t >= 35.0f && t <= 85.0f) reqSet = (byte)round(t);
  }

  this->handleStateUpdateRequest(requestedState, reqMode, reqFan, reqSet);
}

}  // namespace midea_dehum
}  // namespace esphome
