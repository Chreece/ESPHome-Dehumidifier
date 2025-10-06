#pragma once

#include <Arduino.h>  // for byte, String, boolean
#include <string>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  // Wiring / attachments
  void set_uart(esphome::uart::UARTComponent *uart);
  void set_error_sensor(sensor::Sensor *s);
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);

  // Lifecycle
  void setup() override;
  void loop() override;

  // Climate interface
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  // Protocol + state handling
  void parseState();
  void clearRxBuf();
  void clearTxBuf();
  void handleUart();
  void writeHeader(byte msgType, byte agreementVersion, byte packetLength);
  void handleStateUpdateRequest(String requestedState, std::string mode, byte fanSpeed, byte humiditySetpoint);
  void sendSetStatus();
  void updateAndSendNetworkStatus(boolean isConnected);
  void getStatus();
  void sendMessage(byte msgType, byte agreementVersion, byte payloadLength, byte *payload);
  void publishState();

 protected:
  esphome::uart::UARTComponent *uart_{nullptr};
  sensor::Sensor *error_sensor_{nullptr};
  binary_sensor::BinarySensor *bucket_full_sensor_{nullptr};
};

}  // namespace midea_dehum
}  // namespace esphome
