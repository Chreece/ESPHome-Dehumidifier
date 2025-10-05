#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace midea_dehum {

// ===== Enumerations and Structs (as in protocol) ============================
typedef enum : byte {
  smart          = 0x00,
  setpoint       = 0x01,
  continuous     = 0x02,
  clothesDrying  = 0x03
} dehumMode_t;

typedef enum : byte {
  low    = 0x20,
  medium = 0x40,
  high   = 0x60
} fanSpeed_t;

typedef struct {
  bool        powerOn;
  dehumMode_t mode;
  fanSpeed_t  fanSpeed;
  byte        humiditySetpoint;
  byte        currentHumidity;
  byte        errorCode;
} dehumidifierState_t;

// ===== Class Declaration ====================================================
class MideaDehumComponent : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  ~MideaDehumComponent() override;

  void set_uart(esphome::uart::UARTComponent *uart);

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // ==== Protocol function set (names 1:1 from reference) ====
  void parseState();
  void clearRxBuf();
  void clearTxBuf();
  void handleUart();
  void writeHeader(byte msgType, byte agreementVersion, byte packetLength);
  void handleStateUpdateRequest(String requestedState, String mode, String fanSpeed, byte humiditySetpoint);
  void sendSetStatus();
  void updateSetStatus(boolean powerOn, dehumMode_t dehumMode, fanSpeed_t fanSpeed, byte humiditySetpoint);
  void updateAndSendNetworkStatus(boolean isConnected);
  void getStatus();
  void updateNetworkStatus(boolean isConnected);
  void sendMessage(byte msgType, byte agreementVersion, byte payloadLength, byte *payload);

  // ==== ESPHome-specific bridging ====
  void publishState();
  void resetWifiSettingsAndReboot();

 protected:
  esphome::uart::UARTComponent *uart_{nullptr};
  sensor::Sensor *error_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
