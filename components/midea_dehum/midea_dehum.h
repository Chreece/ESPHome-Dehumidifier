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

  // Climate API
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

 protected:
  // UART helpers
  void parse_frame_(const std::vector<uint8_t> &frame);
  void send_command_(const std::vector<uint8_t> &cmd);
  uint8_t checksum_(const std::vector<uint8_t> &bytes);

  std::vector<uint8_t> rx_buf_;
};

}  // namespace midea_dehum
}  // namespace esphome
