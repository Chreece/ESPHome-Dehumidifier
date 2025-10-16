#pragma once

#include <cstdint>
#include <string>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#ifdef USE_MIDEA_DEHUM_BINARY_SENSOR
  #include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_MIDEA_DEHUM_SENSOR
  #include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_MIDEA_DEHUM_SWITCH
  #include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace midea_dehum {

#ifdef USE_MIDEA_DEHUM_SWITCH
class MideaDehumComponent;
#endif
#ifdef USE_MIDEA_DEHUM_ION
class MideaIonSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif
#ifdef USE_MIDEA_DEHUM_SWITCH
class MideaSwingSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(class MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  class MideaDehumComponent *parent_{nullptr};
};
#endif

class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  void set_uart(uart::UARTComponent *uart);

#ifdef USE_MIDEA_DEHUM_ERROR
  void set_error_sensor(sensor::Sensor *s);
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);
#endif

#ifdef USE_MIDEA_DEHUM_ION
  void set_ion_switch(MideaIonSwitch *s);
  void set_ion_state(bool on);
  bool get_ion_state() const { return this->ion_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  void set_swing_switch(MideaSwingSwitch *s);
  void set_swing_state(bool on);
  bool get_swing_state() const { return this->swing_state_; }
#endif

  std::string display_mode_setpoint_{"Setpoint"};
  std::string display_mode_continuous_{"Continuous"};
  std::string display_mode_smart_{"Smart"};
  std::string display_mode_clothes_drying_{"ClothesDrying"};

  void set_display_mode_setpoint(const std::string &name) { display_mode_setpoint_ = name; }
  void set_display_mode_continuous(const std::string &name) { display_mode_continuous_ = name; }
  void set_display_mode_smart(const std::string &name) { display_mode_smart_ = name; }
  void set_display_mode_clothes_drying(const std::string &name) { display_mode_clothes_drying_ = name; }

  void setup() override;
  void loop() override;

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void publishState();
  void parseState();
  void sendSetStatus();
  void handleUart();
  void handleStateUpdateRequest(std::string requested_state,
                                uint8_t mode,
                                uint8_t fan_speed,
                                uint8_t humidity_setpoint);
  void updateAndSendNetworkStatus();
  void getStatus();
  void sendMessage(uint8_t msg_type,
                   uint8_t agreement_version,
                   uint8_t payload_length,
                   uint8_t *payload);

 protected:
  void clearRxBuf();
  void clearTxBuf();
  void writeHeader(uint8_t msg_type,
                   uint8_t agreement_version,
                   uint8_t packet_length);

  uart::UARTComponent *uart_{nullptr};

#ifdef USE_MIDEA_DEHUM_ERROR
  sensor::Sensor *error_sensor_{nullptr};
#endif

#ifdef USE_MIDEA_DEHUM_BUCKET
  binary_sensor::BinarySensor *bucket_full_sensor_{nullptr};
#endif

#ifdef USE_MIDEA_DEHUM_ION
  MideaIonSwitch *ion_switch_{nullptr};
  bool ion_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_SWITCH
  MideaSwingSwitch *swing_switch_{nullptr};
  bool swing_state_{false};
#endif
 private:
  bool last_wifi_connected_{false};
};

}  // namespace midea_dehum
}  // namespace esphome
