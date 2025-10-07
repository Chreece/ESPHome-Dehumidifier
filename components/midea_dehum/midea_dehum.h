#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace sensor { class Sensor; }
namespace switch_ { class Switch; }
namespace midea_dehum {

class MideaDehumComponent;

class MideaIonSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { parent_ = parent; }
 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  void set_uart(esphome::uart::UARTComponent *uart);
  void set_error_sensor(sensor::Sensor *s);
  void init_internal_error_sensor();
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);
  void set_ion_switch(MideaIonSwitch *s);

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

  void set_ion_state(bool on);
  bool get_ion_state() const { return this->ion_state_; }

 protected:
  esphome::uart::UARTComponent *uart_{nullptr};
  sensor::Sensor *error_sensor_{nullptr};
  sensor::Sensor *internal_error_sensor_{nullptr};
  binary_sensor::BinarySensor *bucket_full_sensor_{nullptr};
  MideaIonSwitch *ion_switch_{nullptr};
  bool ion_state_{false};
};

}  // namespace midea_dehum
}  // namespace esphome
