#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace midea_dehum {

class MideaDehumComponent : public climate::Climate, public uart::UARTDevice {
 public:
  MideaDehumComponent(uart::UARTComponent *parent) : uart::UARTDevice(parent) {}

 protected:
  void control(const climate::ClimateCall &call) override {
    // Example: just log calls
    ESP_LOGI("midea_dehum", "Control received");
  }
};

}  // namespace midea_dehum
}  // namespace esphome
