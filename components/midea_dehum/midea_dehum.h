#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate, public Component, public uart::UARTDevice {
 public:

  void set_uart(esphome::uart::UARTComponent *uart);

  inline void set_error_sensor(sensor::Sensor *s) { this->error_sensor_ = s; }
  void set_bucket_full_sensor(binary_sensor::BinarySensor *sensor) { this->bucket_full_sensor_ = sensor; }

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

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
