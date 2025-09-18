#pragma once

#include "esphome.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public Component, public UARTDevice {
 public:
  explicit MideaDehumComponent(UARTComponent *parent) : UARTDevice(parent) {}

  void setup() override;
  void loop() override;

  // Commands from HA
  void set_power(bool state);
  void set_mode(const std::string &mode);
  void set_fan(const std::string &fan);
  void set_target_humidity(int humidity);
  void set_swing(bool state);
  void set_ion(bool state);

  // Publishing states (if using C++-side entities or for templates)
  void publish_current_humidity(float value);
  void publish_error(const std::string &err);
  void publish_tank_full(bool state);
  void publish_power(bool state);
  void publish_mode(const std::string &mode);
  void publish_fan(const std::string &fan);
  void publish_swing(bool state);
  void publish_ion(bool state);

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  uint8_t calculate_checksum_(const std::vector<uint8_t> &cmd);

  std::vector<uint8_t> rx_buffer_;
};

}  // namespace midea_dehum
}  // namespace esphome
