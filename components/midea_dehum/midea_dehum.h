#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
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
#ifdef USE_MIDEA_DEHUM_BUTTON
#include "esphome/components/button/button.h"
#endif

#include "midea_dehum_protocol.h"
#include "midea_dehum_protocol_auto.h"

namespace esphome {
namespace midea_dehum {

// ── Shared data structure ──────────────────────────────────────────────────

struct DehumidifierState {
  bool powerOn;
  uint8_t mode;
  uint8_t fanSpeed;
  uint8_t humiditySetpoint;
  uint8_t currentHumidity;
  float currentTemperature;
};

// ── Forward declarations ───────────────────────────────────────────────────

class MideaDehumComponent;

// ── Feature helper classes ─────────────────────────────────────────────────
//    Implemented in midea_dehum_features.cpp

#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
class MideaFilterCleanedButton : public button::Button, public Component {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void press_action() override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
class MideaResetWaterLevelButton : public button::Button, public Component {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void press_action() override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_ION
class MideaIonSwitch : public switch_::Switch, public Component {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void write_state(bool state) override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_PUMP
class MideaPumpSwitch : public esphome::switch_::Switch {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void write_state(bool state) override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_BEEP
class MideaBeepSwitch : public switch_::Switch, public Component {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void write_state(bool state) override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_SLEEP
class MideaSleepSwitch : public switch_::Switch, public Component {
public:
  void set_parent(MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void write_state(bool state) override;
  MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_CAPABILITIES
class MideaCapabilitiesTextSensor : public text_sensor::TextSensor, public Component {
public:
  void set_parent(class MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  class MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_PROTOCOL
class MideaProtocolTextSensor : public text_sensor::TextSensor, public Component {
public:
  void set_parent(class MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  class MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_TIMER
class MideaTimerNumber : public number::Number, public Component {
public:
  void set_parent(class MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void control(float value) override;
  class MideaDehumComponent* parent_{nullptr};
};
#endif

#ifdef USE_MIDEA_DEHUM_TARGET_HUMIDITY
class MideaTargetHumidityNumber : public number::Number, public Component {
public:
  void set_parent(class MideaDehumComponent* parent) { this->parent_ = parent; }

protected:
  void control(float value) override;
  class MideaDehumComponent* parent_{nullptr};
};
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  Main component
// ═══════════════════════════════════════════════════════════════════════════

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice, public Component {
public:
  // ── A. Config / wiring — called from Python (__init__.py) ────────────

  void set_status_poll_interval(uint32_t interval_ms) { this->status_poll_interval_ = interval_ms; }
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  void set_handshake_enabled(bool enabled) { this->handshake_enabled_ = enabled; }
#endif
#ifdef USE_MIDEA_DEHUM_ERROR
  void set_error_sensor(sensor::Sensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
  void set_tank_level_sensor(sensor::Sensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_PM25
  void set_pm25_sensor(sensor::Sensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_HUMIDITY
  void set_humidity_sensor(sensor::Sensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_TEMPERATURE
  void set_temperature_sensor(sensor::Sensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  void set_bucket_full_sensor(binary_sensor::BinarySensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_DEFROST
  void set_defrost_sensor(binary_sensor::BinarySensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_FILTER
  void set_filter_request_sensor(binary_sensor::BinarySensor* s);
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  void set_filter_cleaned_button(MideaFilterCleanedButton* b);
  void set_filter_cleaned_flag(bool flag) { this->filter_cleaned_flag_ = flag; }
  bool is_filter_request_active() const { return this->filter_request_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
  void set_reset_water_level_button(MideaResetWaterLevelButton* b);
  void sendResetWaterLevel();
#endif
#ifdef USE_MIDEA_DEHUM_ION
  void set_ion_switch(MideaIonSwitch* s);
  void set_ion_state(bool on);
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  MideaPumpSwitch* pump_switch_{nullptr};
  bool pump_state_{false};

  void set_pump_switch(MideaPumpSwitch* s);
  void set_pump_state(bool on);
#endif
#ifdef USE_MIDEA_DEHUM_BEEP
  MideaBeepSwitch* beep_switch_{nullptr};
  bool beep_state_{false};
  void set_beep_switch(MideaBeepSwitch* s);
  void set_beep_state(bool on);
  void restore_beep_state();
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  MideaSleepSwitch* sleep_switch_{nullptr};
  bool sleep_state_{false};
  void set_sleep_switch(MideaSleepSwitch* s);
  void set_sleep_state(bool on);
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  void set_capabilities_text_sensor(MideaCapabilitiesTextSensor* sens) {
    this->capabilities_text_ = sens;
  }
  void update_capabilities_text(const std::vector<std::string>& options);
  void processCapabilitiesPacket(uint8_t* data, size_t length);
  void getDeviceCapabilities();
  void getDeviceCapabilitiesMore();
#endif
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  void set_protocol_text_sensor(MideaProtocolTextSensor* sens) { this->protocol_text_ = sens; }
  void publish_protocol_text();
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  void set_timer_number(MideaTimerNumber* n);
  void set_timer_hours(float hours, bool from_device);
#endif
#ifdef USE_MIDEA_DEHUM_TARGET_HUMIDITY
  void set_target_humidity_number(MideaTargetHumidityNumber* n);
#endif

  // ── B. Feature interface — sensors, switches, buttons ─────────────────
  //    Wire‑up stubs implemented in midea_dehum_features.cpp

  std::string display_mode_setpoint_{"Setpoint"};
  std::string display_mode_continuous_{"Continuous"};
  std::string display_mode_smart_{"Smart"};
  std::string display_mode_clothes_drying_{"ClothesDrying"};

  void set_display_mode_setpoint(const std::string& name) { display_mode_setpoint_ = name; }
  void set_display_mode_continuous(const std::string& name) { display_mode_continuous_ = name; }
  void set_display_mode_smart(const std::string& name) { display_mode_smart_ = name; }
  void set_display_mode_clothes_drying(const std::string& name) {
    display_mode_clothes_drying_ = name;
  }

  // ── C. Feature‑flag accessors — for protocol vtable callbacks ─────────

  const DehumidifierState& get_state() const { return this->state_; }
#ifdef USE_MIDEA_DEHUM_BEEP
  bool get_beep_state() const { return this->beep_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_ION
  bool get_ion_state() const { return this->ion_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  bool get_sleep_state() const { return this->sleep_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  bool get_pump_state() const { return this->pump_state_; }
#endif
#ifdef USE_MIDEA_DEHUM_LIGHT
  uint8_t get_light_class() const { return this->light_class_; }
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  bool get_swing_state() const { return this->swing_state_; }
#endif
  uint8_t get_tank_level() const { return this->tank_level_; }
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  bool pop_filter_cleaned_flag() {
    bool f                     = this->filter_cleaned_flag_;
    this->filter_cleaned_flag_ = false;
    return f;
  }
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  uint8_t get_last_on_raw() const { return this->last_on_raw_; }
  uint8_t get_last_off_raw() const { return this->last_off_raw_; }
  uint8_t get_last_ext_raw() const { return this->last_ext_raw_; }
  bool get_timer_write_pending() const { return this->timer_write_pending_; }
  float get_pending_timer_hours() const { return this->pending_timer_hours_; }
  bool get_pending_applies_to_on() const { return this->pending_applies_to_on_; }
  void set_last_on_raw(uint8_t v) { this->last_on_raw_ = v; }
  void set_last_off_raw(uint8_t v) { this->last_off_raw_ = v; }
  void set_last_ext_raw(uint8_t v) { this->last_ext_raw_ = v; }
  void clear_timer_write_pending() { this->timer_write_pending_ = false; }
#endif

  // ── 1. ESPHome lifecycle ────────────────────────────────────────────────
  void setup() override;
  void loop() override;

  // ── 2. RX dispatch (called from midea_dehum_uart.cpp) ───────────────────
  void performHandshakeStep();
  void processPacket(uint8_t* data, size_t len);  // protected, declared here for clarity

  // ── 3. Home Assistant climate control ────────────────────────────────────
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall& call) override;
  void handleStateUpdateRequest(bool power_on, uint8_t mode, uint8_t fan_speed,
                                uint8_t humidity_setpoint);

  // ── 4. TX dispatch ──────────────────────────────────────────────────────
  void sendSetStatus();

  // ── 5. HA state publishing ──────────────────────────────────────────────
  void sendClimateState();

  // ── 6. Status decoding (implemented in midea_dehum_state.cpp) ───────────
  void parseState(const uint8_t* buf);

  // ── 7. Protocol wiring ──────────────────────────────────────────────────
  void set_protocol_version(uint8_t version);
  void switch_protocol(const ProtocolVTable* proto);
  bool is_auto_detect() const { return ad_is_active(this); }
  void protocol_auto_next();

  // ── 8. UART I/O ─────────────────────────────────────────────────────────
  void set_uart(esphome::uart::UARTComponent* uart);
  void handleUart();
  void clearRxBuf();

  // Low‑level TX — called by protocol code and features
  void sendMessage(uint8_t msg_type, uint8_t agreement_version, uint8_t frame_SyncCheck,
                   uint8_t payload_length, uint8_t* payload);
  void updateAndSendNetworkStatus(bool connected);
  void getStatus();

  // ── Protocol vtable accessors (used by protocol_v{1,2}.cpp callbacks) ───
  void set_handshake_step(uint8_t s) { this->handshake_step_ = s; }
  uint8_t get_handshake_step() const { return this->handshake_step_; }
  void set_handshake_done(bool d) { this->handshake_done_ = d; }
  bool get_handshake_done() const { return this->handshake_done_; }
  void set_appliance_type(uint8_t t) { this->appliance_type_ = t; }
  void set_mcu_protocol_version(uint8_t v) { this->mcu_protocol_version_ = v; }
  uint8_t get_mcu_protocol_version() const { return this->mcu_protocol_version_; }
  void set_device_info_known(bool k) { this->device_info_known_ = k; }
  bool is_device_info_known() const { return this->device_info_known_; }
  void set_protocol_ptr(const ProtocolVTable* p) { this->protocol_ = p; }
  const ProtocolVTable* get_protocol_ptr() const { return this->protocol_; }

  // Returns true if V2 is the active protocol (fixed V2, or auto-detected V2).
  bool is_v2_active() const {
    return this->user_protocol_version_ == 2 ||
           (this->protocol_ != nullptr && this->protocol_->version == 2);
  }

  // ── Protocol vtable + auto‑detect state (public, accessed by protocol_auto.cpp free fns) ──
  const ProtocolVTable* protocol_{nullptr};
  uint8_t user_protocol_version_{0};  // 0=auto, 1=V1, 2=V2
  MideaAutoDetect ad_state_;

protected:
  // ═══════════════════════════════════════════════════════════════════════
  //  Protected members — grouped by source file
  // ═══════════════════════════════════════════════════════════════════════

  // ── Runtime state (midea_dehum.cpp) ──────────────────────────────────
  DehumidifierState state_{false, 3, 60, 50, 0, 0.0f};
  bool first_run_{true};  // cleared after first successful parseState()

#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  uint8_t handshake_step_{0};
  bool handshake_enabled_{true};
  bool handshake_done_{false};
#endif

  // ── Packet framing (midea_dehum_uart.cpp) ────────────────────────────
  void clearTxBuf();
  void writeHeader(uint8_t msg_type, uint8_t agreement_version, uint8_t frame_SyncCheck,
                   uint8_t packet_length);

  enum BusState { BUS_IDLE, BUS_RECEIVING, BUS_SENDING };

  // ── Device identity (midea_dehum.cpp) ────────────────────────────────
  uint8_t appliance_type_       = 0xa1;
  uint8_t mcu_protocol_version_ = 0x00;  // from MCU data[7]
  bool device_info_known_       = false;

  // ── External dependencies ────────────────────────────────────────────
  uart::UARTComponent* uart_{nullptr};
  size_t rx_len_{0};  // reusable across loops (was static in handleUart)
  uint32_t status_poll_interval_{1000};

  // ── Feature state (midea_dehum_features.cpp, midea_dehum_state.cpp) ───

#ifdef USE_MIDEA_DEHUM_ERROR
  sensor::Sensor* error_sensor_{nullptr};
#endif
#if defined(USE_MIDEA_DEHUM_ERROR) || defined(USE_MIDEA_DEHUM_BUCKET)
  uint8_t error_state_{0};
#endif
#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
  sensor::Sensor* tank_level_sensor_{nullptr};
#endif
  uint8_t tank_level_{0};  // always parsed from status byte 20 (V2 needs it for cmd[15])
#ifdef USE_MIDEA_DEHUM_PM25
  sensor::Sensor* pm25_sensor_{nullptr};
  uint16_t pm25_{0};
#endif
#ifdef USE_MIDEA_DEHUM_HUMIDITY
  sensor::Sensor* humidity_sensor_{nullptr};
  uint8_t last_published_humidity_{0};
#endif
#ifdef USE_MIDEA_DEHUM_TEMPERATURE
  sensor::Sensor* temperature_sensor_{nullptr};
  float last_published_temp_{-100.0f};
#endif
#ifdef USE_MIDEA_DEHUM_BUCKET
  binary_sensor::BinarySensor* bucket_full_sensor_{nullptr};
  bool bucket_full_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_DEFROST
  binary_sensor::BinarySensor* defrost_sensor_{nullptr};
  bool defrost_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_FILTER
  binary_sensor::BinarySensor* filter_request_sensor_{nullptr};
  bool filter_request_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  button::Button* filter_cleaned_button_{nullptr};
  bool filter_cleaned_flag_{false};
#endif
#ifdef USE_MIDEA_DEHUM_RESET_WATER_LEVEL
  button::Button* reset_water_level_button_{nullptr};
#endif
#ifdef USE_MIDEA_DEHUM_ION
  MideaIonSwitch* ion_switch_{nullptr};
  bool ion_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_LIGHT
  esphome::select::Select* light_select_{nullptr};
  uint8_t light_class_{0};
#endif
#ifdef USE_MIDEA_DEHUM_SWING
  bool swing_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_HORIZONTAL_SWING
  bool horizontal_swing_state_{false};
#endif
#ifdef USE_MIDEA_DEHUM_TIMER
  MideaTimerNumber* timer_number_{nullptr};
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
#ifdef USE_MIDEA_DEHUM_TARGET_HUMIDITY
  MideaTargetHumidityNumber* target_humidity_number_{nullptr};
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  MideaCapabilitiesTextSensor* capabilities_text_{nullptr};
  bool capabilities_requested_{false};
#endif
#ifdef USE_MIDEA_DEHUM_PROTOCOL
  MideaProtocolTextSensor* protocol_text_{nullptr};
  std::string last_protocol_str_;
#endif
};

}  // namespace midea_dehum
}  // namespace esphome
