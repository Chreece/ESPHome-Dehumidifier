#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace midea_dehum {

// ===== Enumerations and Structs (as in protocol) ============================
enum fanSpeed_t {
  low = 40,
  medium = 60,
  high = 80
};

enum dehumMode_t {
  setpoint = 1,
  continuous = 2,
  smart = 3,
  clothesDrying = 4
};

struct dehumidifierState_t { 
  boolean powerOn;
  dehumMode_t mode;
  fanSpeed_t fanSpeed;
  byte humiditySetpoint;
  byte currentHumidity;
  byte errorCode;
};
// ===== Class Declaration ====================================================
class MideaDehumComponent : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  ~MideaDehumComponent();

  void set_uart(esphome::uart::UARTComponent *uart);

  inline void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }

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

 protected:
  esphome::uart::UARTComponent *uart_{nullptr};
  sensor::Sensor *error_sensor_{nullptr};  
  bool network_initialized_ = false;
};

}  // namespace midea_dehum
}  // namespace esphome
