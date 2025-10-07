#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_MIDEA_DEHUM_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#ifdef USE_MIDEA_DEHUM_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace midea_dehum {

class MideaIonSwitch;

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  void set_uart(esphome::uart::UARTComponent *uart);
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);

#ifdef USE_MIDEA_DEHUM_SENSOR
  void set_error_sensor(sensor::Sensor *s);
#endif

#ifdef USE_MIDEA_DEHUM_SWITCH
  void set_ion_switch(MideaIonSwitch *s);
  void set_ion_state(bool on);
  bool get_ion_state() const { return this->ion_state_; }
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

 protected:
  esphome::uart::UARTComponent *uart_{nullptr};
  binary_sensor::BinarySensor *bucket_full_sensor_{nullptr};

#ifdef USE_MIDEA_DEHUM_SENSOR
  sensor::Sensor *error_sensor_{nullptr};
#endif

#ifdef USE_MIDEA_DEHUM_SWITCH
  MideaIonSwitch *ion_switch_{nullptr};
  bool ion_state_{false};
#endif
};

#ifdef USE_MIDEA_DEHUM_SWITCH
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
