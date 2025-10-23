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
#ifdef USE_MIDEA_DEHUM_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_MIDEA_DEHUM_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_MIDEA_DEHUM_TEXT
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent;
#ifdef USE_MIDEA_DEHUM_ION
class MideaIonSwitch;
#endif
#ifdef USE_MIDEA_DEHUM_SWING
class MideaSwingSwitch;
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
class MideaPumpSwitch;
#endif
#ifdef USE_MIDEA_DEHUM_BEEP
class MideaBeepSwitch;
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
class MideaSleepSwitch;
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

#ifdef USE_MIDEA_DEHUM_SWING
class MideaSwingSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_PUMP
class MideaPumpSwitch : public esphome::switch_::Switch {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }
 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_BEEP
class MideaBeepSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_SLEEP
class MideaSleepSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  MideaDehumComponent *parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_CAPABILITIES
class MideaCapabilitiesTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void set_parent(class MideaDehumComponent *parent) { this->parent_ = parent; }

  void update_capabilities(const std::vector<std::string> &options);

 protected:
  class MideaDehumComponent *parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_TIMER
class MideaTimerNumber : public number::Number, public Component {
 public:
  void set_parent(class MideaDehumComponent *parent) { this->parent_ = parent; }

 protected:
  void control(float value) override;
  class MideaDehumComponent *parent_{nullptr};
};
#endif

// ─────────────── Main component ───────────────
class MideaDehumComponent : public climate::Climate,
                            public uart::UARTDevice,
                            public Component {
 public:
  void set_uart(uart::UARTComponent *uart);
  void set_status_poll_interval(uint32_t interval_ms) { this->status_poll_interval_ = interval_ms; }

#ifdef USE_MIDEA_DEHUM_ERROR
  void set_error_sensor(sensor::Sensor *s);
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  void set_bucket_full_sensor(binary_sensor::BinarySensor *s);
#endif
#ifdef USE_MIDEA_DEHUM_ION
  void set_ion_switch(MideaIonSwitch *s);
  void set_ion_state(bool on, bool from_device);
  bool get_ion_state() const { return this->ion_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  void set_swing_switch(MideaSwingSwitch *s);
  void set_swing_state(bool on, bool from_device);
  bool get_swing_state() const { return this->swing_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  MideaPumpSwitch *pump_switch_{nullptr};
  bool pump_state_{false};

  void set_pump_switch(MideaPumpSwitch *s);
  void set_pump_state(bool on, bool from_device);
#endif
#ifdef USE_MIDEA_DEHUM_BEEP
  MideaBeepSwitch *beep_switch_{nullptr};
  bool beep_state_{false};
  void set_beep_switch(MideaBeepSwitch *s);
  void set_beep_state(bool on);
  void restore_beep_state();
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  MideaSleepSwitch *sleep_switch_{nullptr};
  bool sleep_state_{false};
  void set_sleep_switch(MideaSleepSwitch *s);
  void set_sleep_state(bool on, bool from_device);
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  void set_capabilities_text_sensor(MideaCapabilitiesTextSensor *sens) { this->capabilities_text_ = sens; }
  void update_capabilities_text(const std::vector<std::string> &options);
  void getDeviceCapabilities();
  void getDeviceCapabilitiesMore();
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  void set_timer_number(MideaTimerNumber *n);
  void set_timer_hours(float hours, bool from_device);
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
  void updateAndSendNetworkStatus(bool connected);
  void getStatus();
  void sendMessage(uint8_t msg_type,
                   uint8_t agreement_version,
                   uint8_t frame_SyncCheck,
                   uint8_t payload_length,
                   uint8_t *payload);

 protected:
  void clearRxBuf();
  void clearTxBuf();
  void writeHeader(uint8_t msg_type,
                   uint8_t agreement_version,
                   uint8_t frame_SyncCheck,
                   uint8_t packet_length);
  void performHandshakeStep();
  uint8_t handshake_step_{0};
  bool handshake_done_{false};

  enum BusState {
    BUS_IDLE,
    BUS_RECEIVING,
    BUS_SENDING
  };

  BusState bus_state_ = BUS_IDLE;
  uint32_t last_rx_time_ = 0;
  bool tx_pending_ = false;
  std::vector<uint8_t> tx_buffer_;

  void queueTx(const uint8_t *data, size_t len);
  void sendQueuedPacket();
  void processPacket(uint8_t *data, size_t len);

  uint8_t appliance_type_ = 0x00;
  uint8_t protocol_version_ = 0x00;
  bool device_info_known_ = false;

  uart::UARTComponent *uart_{nullptr};
  uint32_t status_poll_interval_{30000};

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
#ifdef USE_MIDEA_DEHUM_SWING
  MideaSwingSwitch *swing_switch_{nullptr};
  bool swing_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  MideaTimerNumber *timer_number_{nullptr};
  float last_timer_hours_{0.0f};

  uint8_t last_on_raw_{0};
  uint8_t last_off_raw_{0};
  uint8_t last_ext_raw_{0};

  bool timer_write_pending_{false};
  float pending_timer_hours_{0.0f};
  bool pending_applies_to_on_{false};
  uint8_t timer_on_raw_{0};
  uint8_t timer_off_raw_{0};
  uint8_t timer_ext_raw_{0};
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  MideaCapabilitiesTextSensor *capabilities_text_{nullptr};
  bool capabilities_requested_{false};
#endif

};

}  // namespace midea_dehum
}  // namespace esphome
