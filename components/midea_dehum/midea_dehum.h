#pragma once

#include "esphome.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include <vector>

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice {
 public:
  explicit MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

  void setup();
  void loop();
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;

  climate::ClimateMode mode = climate::CLIMATE_MODE_OFF;
  int current_humidity = 0;
  int target_humidity = 50;
};

}  // namespace midea_dehum
}  // namespace esphome
