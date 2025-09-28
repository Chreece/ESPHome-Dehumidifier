#pragma once

#include "esphome.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace midea_dehum {

// Simple climate-based dehumidifier using UART protocol:
// Frame: [0xAA][CMD][DATA][CHECKSUM=sum of all previous bytes mod 256]
class MideaDehumComponent : public climate::Climate, public UARTDevice {
 public:
  explicit MideaDehumComponent(UARTComponent *parent) : UARTDevice(parent) {}

  void setup() override;
  void loop() override;

<<<<<<< HEAD
  // Climate API
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;
=======
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
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e

 protected:
  // UART helpers
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
<<<<<<< HEAD
  uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;
=======
  uint8_t calculate_checksum_(const std::vector<uint8_t> &cmd);

  std::vector<uint8_t> rx_buffer_;
>>>>>>> 7f4d62cd5bb9d7e19c72e84b86286ad9604f955e
};

}  // namespace midea_dehum
}  // namespace esphome
