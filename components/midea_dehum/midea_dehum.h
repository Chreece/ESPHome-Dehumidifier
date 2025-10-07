#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"       // always included
#include "esphome/components/binary_sensor/binary_sensor.h"

#if USE_MIDEA_DEHUM_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace midea_dehum {

class MideaIonSwitch;  // forward declaration if switch enabled

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
 public:
  void set_uart(esphome::uart::UARTComponent *uart);
  void set_error_sensor(sensor::Sensor *s);  // optional external
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);
#if USE_MIDEA_DEHUM_SWITCH
  void set_ion_switch(MideaIonSwitch *s);
#endif

  void setup() override;
  void loop() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void publishState();
  void parseState();
  void sendSetStatus();
  void handleUart();
  void handleStateUpdateRequest(String requestedState, std::string mode, byte fanSpeed, byte humiditySetpoint);
  void updateAndSendNetworkStatus(boolean isConnected);
  void getStatus();
  void sendMessage(byte msgType, byte agreementVersion, byte payloadLength, byte *payload);

#if USE_MIDEA_DEHUM_SWITCH
  void set_ion_state(bool on);
  bool get_ion_state() const { return this->ion_state_; }
#endif

 protected:
  void clearRxBuf();
  void clearTxBuf();
  void writeHeader(byte msgType, byte agreementVersion, byte packetLength);

  esphome::uart::UARTComponent *uart_{nullptr};

  sensor::Sensor internal_error_sensor_;
  sensor::Sensor *error_sensor_{nullptr};
  binary_sensor::BinarySensor *bucket_full_sensor_{nullptr};

#if USE_MIDEA_DEHUM_SWITCH
  MideaIonSwitch *ion_switch_{nullptr};
  bool ion_state_{false};
#endif
};

#if USE_MIDEA_DEHUM_SWITCH
class MideaIonSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }
 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif

}  // namespace midea_dehum
}  // namespace esphome
