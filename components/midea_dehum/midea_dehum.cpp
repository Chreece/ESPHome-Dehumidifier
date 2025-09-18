#include "midea_dehum.h"

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/core/config.h"
#include "esphome/core/helpers.h"

using namespace esphome;

static const char *TAG = "midea_dehum";

// Global instance
MideaDehumComponent *midea_dehum_comp = nullptr;

// ==========================
// Component Setup
// ==========================
void MideaDehumComponent::setup() {
  ESP_LOGI(TAG, "Midea Dehumidifier setup");

  midea_dehum_comp = this;

  // Register entities
  power_switch_ = new Switch();
  power_switch_->set_name("Dehumidifier Power");
  power_switch_->set_icon("mdi:power");
  power_switch_->register_component(this);

  swing_switch_ = new Switch();
  swing_switch_->set_name("Dehumidifier Swing");
  swing_switch_->set_icon("mdi:swap-vertical");
  swing_switch_->register_component(this);

  ion_switch_ = new Switch();
  ion_switch_->set_name("Dehumidifier Ion");
  ion_switch_->set_icon("mdi:air-filter");
  ion_switch_->register_component(this);

  mode_select_ = new Select();
  mode_select_->set_name("Dehumidifier Mode");
  mode_select_->add_option("auto");
  mode_select_->add_option("setpoint");
  mode_select_->add_option("continuous");
  mode_select_->add_option("fan");
  mode_select_->register_component(this);

  fan_select_ = new Select();
  fan_select_->set_name("Dehumidifier Fan Speed");
  fan_select_->add_option("auto");
  fan_select_->add_option("low");
  fan_select_->add_option("medium");
  fan_select_->add_option("high");
  fan_select_->register_component(this);

  target_humidity_ = new Number();
  target_humidity_->set_name("Dehumidifier Target Humidity");
  target_humidity_->set_range(30, 80);
  target_humidity_->set_step(1);
  target_humidity_->register_component(this);

  current_humidity_ = new Sensor();
  current_humidity_->set_name("Dehumidifier Current Humidity");
  current_humidity_->set_unit_of_measurement("%");
  current_humidity_->register_component(this);

  error_sensor_ = new Sensor();
  error_sensor_->set_name("Dehumidifier Error Code");
  error_sensor_->register_component(this);

  tank_full_ = new BinarySensor();
  tank_full_->set_name("Dehumidifier Tank Full");
  tank_full_->register_component(this);
}

// ==========================
// Loop: read UART
// ==========================
void MideaDehumComponent::loop() {
  while (this->available()) {
    uint8_t c = this->read();
    rx_buffer_.push_back(c);

    // Parse frame when buffer has enough data
    if (rx_buffer_.size() >= 10) {
      parse_frame_(rx_buffer_);
      rx_buffer_.clear();
    }
  }
}

// ==========================
// Control Methods
// ==========================
void MideaDehumComponent::set_power(bool state) {
  publish_power(state);
  uint8_t cmd_byte = 0x01;
  std::vector<uint8_t> cmd = {0xAA, cmd_byte, state ? 0x01 : 0x00};
  send_command_(cmd);
}

void MideaDehumComponent::set_mode(const std::string &mode) {
  publish_mode(mode);
  uint8_t mode_byte = 0x00;
  if (mode == "setpoint") mode_byte = 0x01;
  else if (mode == "continuous") mode_byte = 0x02;
  else if (mode == "fan") mode_byte = 0x03;
  std::vector<uint8_t> cmd = {0xAA, 0x02, mode_byte};
  send_command_(cmd);
}

void MideaDehumComponent::set_fan(const std::string &fan) {
  publish_fan(fan);
  uint8_t fan_byte = 0x00;
  if (fan == "low") fan_byte = 0x01;
  else if (fan == "medium") fan_byte = 0x02;
  else if (fan == "high") fan_byte = 0x03;
  std::vector<uint8_t> cmd = {0xAA, 0x03, fan_byte};
  send_command_(cmd);
}

void MideaDehumComponent::set_target_humidity(int humidity) {
  if (humidity < 30) humidity = 30;
  if (humidity > 80) humidity = 80;
  publish_current_humidity(humidity);
  std::vector<uint8_t> cmd = {0xAA, 0x04, static_cast<uint8_t>(humidity)};
  send_command_(cmd);
}

void MideaDehumComponent::set_swing(bool state) {
  publish_swing(state);
  std::vector<uint8_t> cmd = {0xAA, 0x05, state ? 0x01 : 0x00};
  send_command_(cmd);
}

void MideaDehumComponent::set_ion(bool state) {
  publish_ion(state);
  std::vector<uint8_t> cmd = {0xAA, 0x06, state ? 0x01 : 0x00};
  send_command_(cmd);
}

// ==========================
// Publishing Methods
// ==========================
void MideaDehumComponent::publish_current_humidity(float value) { current_humidity_->publish_state(value); }
void MideaDehumComponent::publish_error(const std::string &err) { error_sensor_->publish_state(err); }
void MideaDehumComponent::publish_tank_full(bool state) { tank_full_->publish_state(state); }
void MideaDehumComponent::publish_power(bool state) { power_switch_->publish_state(state); }
void MideaDehumComponent::publish_mode(const std::string &mode) { mode_select_->publish_state(mode); }
void MideaDehumComponent::publish_fan(const std::string &fan) { fan_select_->publish_state(fan); }
void MideaDehumComponent::publish_swing(bool state) { swing_switch_->publish_state(state); }
void MideaDehumComponent::publish_ion(bool state) { ion_switch_->publish_state(state); }

// ==========================
// UART Parsing & Sending
// ==========================
void MideaDehumComponent::parse_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10) return;
  uint8_t checksum = 0;
  for (size_t i = 0; i < frame.size() - 1; ++i) checksum += frame[i];
  if (checksum != frame.back()) return;

  uint8_t power = frame[2] & 0x01;
  uint8_t mode = (frame[3] >> 4) & 0x0F;
  uint8_t fan = (frame[3] >> 2) & 0x03;
  uint8_t humidity = frame[4];
  uint8_t swing = (frame[5] >> 5) & 0x01;
  uint8_t ion = (frame[5] >> 6) & 0x01;
  uint8_t tank_full = (frame[6] >> 7) & 0x01;
  uint8_t error = frame[7];

  publish_power(power);
  publish_mode(mode);
  publish_fan(fan);
  publish_current_humidity(humidity);
  publish_swing(swing);
  publish_ion(ion);
  publish_tank_full(tank_full);
  publish_error(error);
}

void MideaDehumComponent::send_command_(const std::vector<uint8_t> &cmd) {
  std::vector<uint8_t> frame = cmd;
  frame.push_back(calculate_checksum(cmd));
  for (auto b : frame) write(b);
}

uint8_t MideaDehumComponent::calculate_checksum(const std::vector<uint8_t> &cmd) {
  uint8_t sum = 0;
  for (auto b : cmd) sum += b;
  return sum;
}

namespace esphome {
namespace midea_dehum {

static const char *TAG = "midea_dehum";

// Define CONFIG_SCHEMA for YAML
static const auto CONFIG_SCHEMA = esphome::config_validation::CONFIG_SCHEMA.extend({
    esphome::config_validation::Required("uart_id"): esphome::config_validation::use_id(UARTComponent),
}).extend(Component::CONFIG_SCHEMA);

// This function is called when parsing YAML
void to_code(const esphome::Config &config) {
  auto *uart = config.get<UARTComponent *>("uart_id");
  auto *comp = new ::MideaDehumComponent(uart);

  App.register_component(comp);
}

}  // namespace midea_dehum
}  // namespace esphome
✅
