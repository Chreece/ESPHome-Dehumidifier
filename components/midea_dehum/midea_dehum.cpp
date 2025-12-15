#include "midea_dehum.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/preferences.h"
#include "esphome/core/version.h"
#include <cmath>

namespace esphome {
namespace midea_dehum {

static const char *const TAG = "midea_dehum";

static bool first_run = true;

static uint8_t networkStatus[19];
static uint8_t currentHeader[10];
static uint8_t getStatusCommand[21] = {
  0x41, 0x81, 0x00, 0xff, 0x03, 0xff,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03
};

static uint8_t dongleAnnounce[12] = {
  0xAA, 0x0B, 0xFF, 0xF4, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x07, 0x00, 0xFA
};

static uint8_t setStatusCommand[25];
static uint8_t serialRxBuf[256];
static uint8_t serialTxBuf[256];

static const uint8_t crc_table[] = {
  0x00,0x5e,0xbc,0xE2,0x61,0x3F,0xDD,0x83,
  0xC2,0x9C,0x7E,0x20,0xA3,0xFD,0x1F,0x41,
  0x9D,0xC3,0x21,0x7F,0xFC,0xA2,0x40,0x1E,
  0x5F,0x01,0xE3,0xBD,0x3E,0x60,0x82,0xDC,
  0x23,0x7D,0x9F,0xC1,0x42,0x1C,0xFE,0xA0,
  0xE1,0xBF,0x5D,0x03,0x80,0xDE,0x3C,0x62,
  0xBE,0xE0,0x02,0x5C,0xDF,0x81,0x63,0x3D,
  0x7C,0x22,0xC0,0x9E,0x1D,0x43,0xA1,0xFF,
  0x46,0x18,0xFA,0xA4,0x27,0x79,0x9B,0xC5,
  0x84,0xDA,0x38,0x66,0xE5,0xBB,0x59,0x07,
  0xDB,0x85,0x67,0x39,0xBA,0xE4,0x06,0x58,
  0x19,0x47,0xA5,0xFB,0x78,0x26,0xC4,0x9A,
  0x65,0x3B,0xD9,0x87,0x04,0x5A,0xB8,0xE6,
  0xA7,0xF9,0x1B,0x45,0xC6,0x98,0x7A,0x24,
  0xF8,0xA6,0x44,0x1A,0x99,0xC7,0x25,0x7B,
  0x3A,0x64,0x86,0xD8,0x5B,0x05,0xE7,0xB9,
  0x8C,0xD2,0x30,0x6E,0xED,0xB3,0x51,0x0F,
  0x4E,0x10,0xF2,0xAC,0x2F,0x71,0x93,0xCD,
  0x11,0x4F,0xAD,0xF3,0x70,0x2E,0xCC,0x92,
  0xD3,0x8D,0x6F,0x31,0xB2,0xEC,0x0E,0x50,
  0xAF,0xF1,0x13,0x4D,0xCE,0x90,0x72,0x2C,
  0x6D,0x33,0xD1,0x8F,0x0C,0x52,0xB0,0xEE,
  0x32,0x6C,0x8E,0xD0,0x53,0x0D,0xEF,0xB1,
  0xF0,0xAE,0x4C,0x12,0x91,0xCF,0x2D,0x73,
  0xCA,0x94,0x76,0x28,0xAB,0xF5,0x17,0x49,
  0x08,0x56,0xB4,0xEA,0x69,0x37,0xD5,0x8B,
  0x57,0x09,0xEB,0xB5,0x36,0x68,0x8A,0xD4,
  0x95,0xCB,0x29,0x77,0xF4,0xAA,0x48,0x16,
  0xE9,0xB7,0x55,0x0B,0x88,0xD6,0x34,0x6A,
  0x2B,0x75,0x97,0xC9,0x4A,0x14,0xF6,0xA8,
  0x74,0x2A,0xC8,0x96,0x15,0x4B,0xA9,0xF7,
  0xB6,0xE8,0x0A,0x54,0xD7,0x89,0x6B,0x35
};

// CRC8 check (second-to-last TX byte)
static uint8_t crc8(uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;
  while (len--) crc = crc_table[*addr++ ^ crc];
  return crc;
}

// Calculate the sum (last TX byte)
static uint8_t checksum(uint8_t *addr, uint8_t len) {
  uint8_t sum = 0;
  addr++;  // skip 0xAA
  while (len--) sum = sum + *addr++;
  return 256 - sum;
}

// Device error sensor
#ifdef USE_MIDEA_DEHUM_ERROR
void MideaDehumComponent::set_error_sensor(sensor::Sensor *s) {
  this->error_sensor_ = s;
}
#endif

// Tank level sensor
#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
void MideaDehumComponent::set_tank_level_sensor(sensor::Sensor *s) {
  this->tank_level_sensor_ = s;
}
#endif

// PM25 sensor
#ifdef USE_MIDEA_DEHUM_PM25
void MideaDehumComponent::set_pm25_sensor(sensor::Sensor *s) {
  this->pm25_sensor_ = s;
}
#endif

// Bucket full sensor
#ifdef USE_MIDEA_DEHUM_BUCKET
void MideaDehumComponent::set_bucket_full_sensor(binary_sensor::BinarySensor *s) { this->bucket_full_sensor_ = s; }
#endif

// Defrost binary sensor
#ifdef USE_MIDEA_DEHUM_DEFROST
void MideaDehumComponent::set_defrost_sensor(binary_sensor::BinarySensor *s) { this->defrost_sensor_ = s; }
#endif

// Filter cleaning request sensor
#ifdef USE_MIDEA_DEHUM_FILTER
void MideaDehumComponent::set_filter_request_sensor(binary_sensor::BinarySensor *s) { this->filter_request_sensor_ = s; }
#endif

// Filter cleaning complete button
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
void MideaDehumComponent::set_filter_cleaned_button(MideaFilterCleanedButton *b) {
  this->filter_cleaned_button_ = b;
  if (auto *btn = dynamic_cast<MideaFilterCleanedButton *>(b)) {
    btn->set_parent(this);
  }
}

void MideaFilterCleanedButton::press_action() {
#ifdef USE_MIDEA_DEHUM_FILTER
  if (this->parent_ == nullptr) return;

  if (this->parent_->is_filter_request_active()) {
    this->parent_->set_filter_cleaned_flag(true);
    this->parent_->sendSetStatus();
  } 
#endif
}
#endif

// Device IONizer
#ifdef USE_MIDEA_DEHUM_ION
void MideaDehumComponent::set_ion_state(bool on) {
  if (this->ion_state_ == on) return;
  this->ion_state_ = on;
  if (this->ion_switch_)
    this->ion_switch_->publish_state(on);
  this->sendSetStatus();
}

void MideaDehumComponent::set_ion_switch(MideaIonSwitch *s) {
  this->ion_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaIonSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_ion_state(state);
}
#endif

// Defrost pump
#ifdef USE_MIDEA_DEHUM_PUMP
void MideaDehumComponent::set_pump_state(bool on) {
  if (this->pump_state_ == on) return;
  this->pump_state_ = on;
  if (this->pump_switch_ != nullptr)
    this->pump_switch_->publish_state(on);
  this->sendSetStatus();
}

void MideaDehumComponent::set_pump_switch(MideaPumpSwitch *s) {
  this->pump_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaPumpSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_pump_state(state);
}
#endif

// Toggle Device Buzzer on commands
#ifdef USE_MIDEA_DEHUM_BEEP
void MideaDehumComponent::set_beep_state(bool on) {
  // Only send if the user requested a change (not just a redundant write)
  bool was = this->beep_state_;
  if (was == on) {
    return;
  }

  this->beep_state_ = on;

  auto pref = global_preferences->make_preference<bool>(0xBEE1234);
  pref.save(&this->beep_state_);

  if (this->beep_switch_) {
    this->beep_switch_->publish_state(this->beep_state_);
  }

  this->sendSetStatus();
}

void MideaDehumComponent::restore_beep_state() {
  auto pref = global_preferences->make_preference<bool>(0xBEE1234);
  bool saved_state = false;
  if (pref.load(&saved_state)) {
    this->beep_state_ = saved_state;
  } else {
    this->beep_state_ = false;
  }

  if (this->beep_switch_) {
    this->beep_switch_->publish_state(this->beep_state_);
  }
}

void MideaDehumComponent::set_beep_switch(MideaBeepSwitch *s) {
  this->beep_switch_ = s;
  if (s) {
    s->set_parent(this);
    s->publish_state(this->beep_state_);
  }
}

void MideaBeepSwitch::write_state(bool state) {
  if (!this->parent_) return;

  this->parent_->set_beep_state(state);
}
#endif

// Set Sleep Switch on device 
#ifdef USE_MIDEA_DEHUM_SLEEP
void MideaDehumComponent::set_sleep_state(bool on) {
  if (this->sleep_state_ == on) return;
  this->sleep_state_ = on;
  if (this->sleep_switch_)
    this->sleep_switch_->publish_state(on);
  this->sendSetStatus();
}


void MideaDehumComponent::set_sleep_switch(MideaSleepSwitch *s) {
  this->sleep_switch_ = s;
  if (s) s->set_parent(this);
}

void MideaSleepSwitch::write_state(bool state) {
  if (!this->parent_) return;
  this->parent_->set_sleep_state(state);
}
#endif

// Get the device capabilities (BETA)
#ifdef USE_MIDEA_DEHUM_CAPABILITIES

struct CapabilityMap {
  uint8_t id;
  uint8_t type;
  const char* name;
};

// Combined table — documented + dehumidifier findings
static const CapabilityMap CAPABILITY_TABLE[] = {
  // ───── Documented standard capabilities ─────
  {0x10, 0x02, "Fan speed control"},
  {0x12, 0x02, "ECO mode"},
  {0x13, 0x02, "8°C heating / freeze protection"},
  {0x14, 0x02, "Mode selection"},
  {0x15, 0x02, "Swing control"},
  {0x16, 0x02, "Power consumption / calibration"},
  {0x17, 0x02, "Filter reminder"},
  {0x18, 0x02, "No-wind comfort / silky cool"},
  {0x19, 0x02, "PTC heater / aux heating"},
  {0x1A, 0x02, "Turbo fan / strong mode"},
  {0x1E, 0x02, "Ionizer"},
  {0x1F, 0x02, "Auto humidity control"},
  {0x21, 0x02, "Filter check"},
  {0x22, 0x02, "Temperature Unit Changeable"},
  {0x24, 0x02, "Display / light control"},
  {0x25, 0x02, "Temperature range"},
  {0x2A, 0x02, "Strong fan (alt)"},
  {0x2C, 0x02, "Buzzer / beep control"},

  // ───── Dehumidifier / B5-specific capabilities ─────
  {0x1D, 0x02, "Drain pump control"},
  {0x20, 0x02, "Clothes Drying"},
  {0x2D, 0x02, "Water level sensor"},

  // ───── Sensor / structural presence capabilities (type 0x00) ─────
  {0x09, 0x00, "Vertical swing support"},
  {0x0A, 0x00, "Horizontal swing support"},
  {0x15, 0x00, "Indoor humidity sensor"},
  {0x18, 0x00, "No wind feel mode"},
  {0x30, 0x00, "Smart eye / energy save on absence"},
  {0x32, 0x00, "Blowing people"},
  {0x33, 0x00, "Avoid people"},
  {0x39, 0x00, "Self clean"},
  {0x42, 0x00, "Prevent direct fan / one-key no wind"},
  {0x43, 0x00, "Breeze control"}
};

void MideaDehumComponent::processCapabilitiesPacket(uint8_t *data, size_t length) {
  if (length < 14) return;
  std::vector<std::string> caps;

  size_t i = 12;
  while (i + 3 < length - 1) {
    uint8_t id   = data[i];
    uint8_t type = data[i + 1];
    uint8_t len  = data[i + 2];
    uint8_t val  = data[i + 3];

    if (i + 3 + len > length) break;

    const char *name = nullptr;
    for (const auto &entry : CAPABILITY_TABLE) {
      if (entry.id == id && entry.type == type) {
        name = entry.name;
        break;
      }
    }

    std::string desc;
    if (name != nullptr) {
      desc = name;

      // Decode multi-valued capabilities
      switch (id) {
        case 0x14: {  // Mode selection
          const char *modes[] = {"Cool", "Heat", "Dry", "Auto"};
          std::string mode_str;
          switch (val) {
            case 0: mode_str = "Cool+Dry+Auto"; break;
            case 1: mode_str = "Cool+Heat+Dry+Auto"; break;
            case 2: mode_str = "Heat+Auto"; break;
            case 3: mode_str = "Cool only"; break;
            default: mode_str = "Unknown"; break;
          }
          desc += " → " + mode_str;
          break;
        }

        case 0x15: {  // Swing control
          const char *swings[] = {"UpDown", "Both", "None", "LeftRight"};
          std::string sw;
          switch (val) {
            case 0: sw = "Up/Down"; break;
            case 1: sw = "Both directions"; break;
            case 2: sw = "None"; break;
            case 3: sw = "Left/Right only"; break;
            default: sw = "Unknown"; break;
          }
          desc += " → " + sw;
          break;
        }

        case 0x16: {  // Power calibration
          switch (val) {
            case 2: desc += " → Calibration supported"; break;
            case 3: desc += " → Calibration+Setting supported"; break;
            default: desc += " → Not supported"; break;
          }
          break;
        }

        case 0x1A: {  // Turbo
          switch (val) {
            case 0: desc += " → Cool only"; break;
            case 1: desc += " → Heat+Cool"; break;
            case 2: desc += " → Disabled"; break;
            case 3: desc += " → Heat only"; break;
            default: desc += " → Unknown"; break;
          }
          break;
        }
        
        case 0x1F: {  // Humidity control
          if (this->device_info_known_ && this->appliance_type_ == 0xA1) {
            // Dehumidifier-specific interpretation
            switch (val) {
              case 0: desc += " → None"; break;
              case 1: desc += " → Manual + Auto"; break;
              case 2: desc += " → val2"; break;
              case 3: desc += " → val3"; break;
              default: desc += " → Unknown"; break;
            }
          } else {
            // Default (AC or other appliance type)
            switch (val) {
              case 0: desc += " → None"; break;
              case 1: desc += " → Auto only"; break;
              case 2: desc += " → Auto+Manual"; break;
              case 3: desc += " → Manual only"; break;
              default: desc += " → Unknown"; break;
            }
          }
          break;
        }

        case 0x25: {  // Temperature range (multi-value)
          if (len >= 6) {
            float min_cool = data[i + 3] / 2.0f;
            float max_cool = data[i + 4] / 2.0f;
            float min_auto = data[i + 5] / 2.0f;
            float max_auto = data[i + 6] / 2.0f;
            float min_heat = data[i + 7] / 2.0f;
            float max_heat = data[i + 8] / 2.0f;
            char buf[64];
            snprintf(buf, sizeof(buf),
                     " → Cool %.1f–%.1f°C, Auto %.1f–%.1f°C, Heat %.1f–%.1f°C",
                     min_cool, max_cool, min_auto, max_auto, min_heat, max_heat);
            desc += buf;
          } else {
            desc += " → Invalid range";
          }
          break;
        }

        default:
          // Generic numeric output for unknown value meanings
          {
            char buf[16];
            snprintf(buf, sizeof(buf), " (val=%u)", val);
            desc += buf;
          }
          break;
      }

      caps.push_back(desc);
    } 
    
    i += 3 + len;
  }

  if (caps.empty())
    caps.push_back("No capabilities detected");

  this->update_capabilities_text(caps);
}

void esphome::midea_dehum::MideaDehumComponent::update_capabilities_text(
  const std::vector<std::string> &options) {

  if (!this->capabilities_text_) return;

  std::string current = this->capabilities_text_->state;
  std::vector<std::string> existing;

  size_t start = 0;
  while (true) {
    size_t comma = current.find(',', start);
    std::string item = current.substr(start, comma - start);

    size_t first = item.find_first_not_of(" \t");
    size_t last = item.find_last_not_of(" \t");
    if (first != std::string::npos && last != std::string::npos)
      item = item.substr(first, last - first + 1);

    if (!item.empty())
      existing.push_back(item);

    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }

  for (const auto &opt : options) {
    bool found = false;
    for (const auto &ex : existing) {
      if (ex == opt) {
        found = true;
        break;
      }
    }
    if (!found) existing.push_back(opt);
  }

  std::string joined;
  for (size_t i = 0; i < existing.size(); i++) {
    joined += existing[i];
    if (i + 1 < existing.size()) joined += ", ";
  }

  this->capabilities_text_->publish_state(joined);
}

// Query device capabilities (B5 command)
void MideaDehumComponent::getDeviceCapabilities() {
  uint8_t payload[] = {
    0xB5,  // Command ID
    0x01,  // Sub-command
    0x00,  // Reserved
    0x00   // Reserved
  };

  this->sendMessage(0x03, 0x03, 0x00, sizeof(payload), payload);
}

// Query additional device capabilities (B5 extended command)
void MideaDehumComponent::getDeviceCapabilitiesMore() {
  uint8_t payload[] = {
    0xB5,  // Command ID
    0x01,  // Sub-command
    0x01,  // Extended request
    0x00,
    0x00
  };

  this->sendMessage(0x03, 0x03, 0x00, sizeof(payload), payload);
}
#endif

// Device internal Timer
#ifdef USE_MIDEA_DEHUM_TIMER
void MideaDehumComponent::set_timer_number(MideaTimerNumber *n) {
  this->timer_number_ = n;
  if (n) {
    n->set_parent(this);
    n->publish_state(this->last_timer_hours_);
  }
}

void MideaDehumComponent::set_timer_hours(float hours, bool from_device) {
  hours = std::clamp(hours, 0.0f, 24.0f);
  this->last_timer_hours_ = hours;

  if (!from_device) {
    this->timer_write_pending_ = true;
    this->pending_timer_hours_ = hours;
    this->pending_applies_to_on_ = !this->state_.powerOn;

    if (this->timer_number_) {
      float current = this->timer_number_->state;
      if (fabs(current - hours) > 0.01f)
        this->timer_number_->publish_state(hours);
    }

    this->sendSetStatus();
  } else {
    if (this->timer_number_) {
      float current = this->timer_number_->state;
      if (fabs(current - hours) > 0.01f)
        this->timer_number_->publish_state(hours);
    }
  }
}

void MideaTimerNumber::control(float value) {
  if (!this->parent_) return;
  this->parent_->set_timer_hours(value, false);
}
#endif

void MideaDehumComponent::set_uart(esphome::uart::UARTComponent *uart) {
  this->set_uart_parent(uart);
  this->uart_ = uart;
}

void MideaDehumComponent::setup() {
#ifdef USE_MIDEA_DEHUM_BEEP
  this->restore_beep_state();
#endif
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  if (this->handshake_enabled_) {
    this->handshake_step_ = 0;
    this->handshake_done_ = false;
    App.scheduler.set_timeout(this, "start_handshake", 2000, [this]() {
      this->performHandshakeStep();
    });
  } else {
    this->handshake_step_ = 2;
    this->handshake_done_ = true;
  }
#else
  this->updateAndSendNetworkStatus(false);
#endif
}

void MideaDehumComponent::loop() {
  this->handleUart();
  
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  if (!this->handshake_done_) {
    return;
  }
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
    bool capabilities_requested_ = false;
    if (!this->capabilities_requested_) {
      this->capabilities_requested_ = true;
      App.scheduler.set_timeout(this, "get_capabilities", 2000, [this]() {
        this->getDeviceCapabilities();
      });
      App.scheduler.set_timeout(this, "get_capabilities_more", 2200, [this]() {
        this->getDeviceCapabilitiesMore();
      });
     }
#endif

  static uint32_t last_status_poll = 0;
  uint32_t now = millis();
  if (now - last_status_poll >= this->status_poll_interval_) {
    last_status_poll = now;
    this->getStatus();
  }
}

void MideaDehumComponent::clearRxBuf() { memset(serialRxBuf, 0, sizeof(serialRxBuf)); }
void MideaDehumComponent::clearTxBuf() { memset(serialTxBuf, 0, sizeof(serialTxBuf)); }

// Handle incoming RX packets
void MideaDehumComponent::handleUart() {
  if (!this->uart_) return;

  static size_t rx_len = 0;

  while (this->uart_->available()) {
    uint8_t byte_in;
    if (!this->uart_->read_byte(&byte_in)) break;

    if (rx_len < sizeof(serialRxBuf)) {
      serialRxBuf[rx_len++] = byte_in;
    } else {
      rx_len = 0;
      continue;
    }

    // Validate start byte
    if (rx_len == 1 && serialRxBuf[0] != 0xAA) {
      rx_len = 0;
      continue;
    }

    // Once length known, check if frame complete
    if (rx_len >= 2) {
      const uint8_t expected_len = serialRxBuf[1];
      if (expected_len < 3 || expected_len > sizeof(serialRxBuf)) {
        rx_len = 0;
        continue;
      }

      if (rx_len >= expected_len) {

        std::vector<uint8_t> local_data(serialRxBuf, serialRxBuf + rx_len);
        this->processPacket(local_data.data(), local_data.size());

        rx_len = 0;
      }
    }
  }
}

// Write the HEADER for all TX packets
void MideaDehumComponent::writeHeader(uint8_t msgType, uint8_t agreementVersion, uint8_t frameSyncCheck, uint8_t packetLength) {
  currentHeader[0] = 0xAA;
  currentHeader[1] = 10 + packetLength + 1;
  currentHeader[2] = this->device_info_known_ ? this->appliance_type_ : 0xA1;
  currentHeader[3] = frameSyncCheck;
  currentHeader[4] = 0x00;
  currentHeader[5] = 0x00;
  currentHeader[6] = 0x00;
  currentHeader[7] = this->device_info_known_ ? this->protocol_version_ : 0x00;
  currentHeader[8] = agreementVersion;
  currentHeader[9] = msgType;
}

#ifdef USE_MIDEA_DEHUM_HANDSHAKE
// Initial Handshakes between Dongle and Device
void MideaDehumComponent::performHandshakeStep() {
  switch (this->handshake_step_) {
    case 0: {
      this->write_array(dongleAnnounce, sizeof(dongleAnnounce));
      this->handshake_step_ = 1;
      break;
    }

    case 1: {
      uint8_t payloadLength = 19;
      uint8_t payload[19];
      memset(payload, 0, sizeof(payload));
      
      this->sendMessage(0xA0, 0x08, 0xBF, payloadLength, payload);
      
      this->handshake_step_ = 2;
      break;
    }

    case 2: {
      this->updateAndSendNetworkStatus(true);
      break;
    }

    default:
      break;
  }
}
#endif
// Process of the RX Packet received
void MideaDehumComponent::processPacket(uint8_t *data, size_t len) {
  // Pretty print packet
  std::string hex_str;
  hex_str.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X ", data[i]);
    hex_str += buf;
  }
  // State response
  if (data[10] == 0xC8) {
    if(!this->device_info_known_){
      this->appliance_type_ = data[2];
      this->protocol_version_ = data[7];
      this->device_info_known_ = true;
    }
    this->parseState();
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
    if(!this->handshake_done_){
      this->handshake_done_ = true;
    }
#endif
  }
#ifdef USE_MIDEA_DEHUM_HANDSHAKE
  // Device ACK response
  else if (data[9] == 0x07 && this->handshake_step_ == 1) {
    this->appliance_type_ = data[2];
    this->protocol_version_ = data[7];
    this->device_info_known_ = true;
    App.scheduler.set_timeout(this, "handshake_step_1", 200, [this]() {
      this->performHandshakeStep();
      this->clearRxBuf();
    });
  }
  // Requested Dongle network status
  else if (data[9] == 0xA0 && this->handshake_step_ == 2) {
    App.scheduler.set_timeout(this, "handshake_step_2", 200, [this]() {
      this->performHandshakeStep();
      this->clearRxBuf();
    });
  }
  // Requested UART ping
  else if (data[9] == 0x05 && !this->handshake_done_) {
    this->write_array(data, data[1] + 1);
    this->handshake_done_ = true;
    
    App.scheduler.set_timeout(this, "post_handshake_init", 1500, [this]() {
      this->getStatus();
    });
  }
#endif
#ifdef USE_MIDEA_DEHUM_CAPABILITIES
  // Capabilities response
  else if (data[10] == 0xB5) {
    this->processCapabilitiesPacket(data, len);
    this->clearRxBuf();
  }
#endif
    
  // Network Status request
  else if (data[10] == 0x63) {
    this->updateAndSendNetworkStatus(true);
    this->clearRxBuf();
  }
  // Reset WIFI request
  else if (
    data[0] == 0xAA &&
    data[9] == 0x64 &&
    data[11] == 0x01 &&
    data[15] == 0x01
  ) {
    this->clearRxBuf();
    App.scheduler.set_timeout(this, "factory_reset", 500, [this]() {
      ESP_LOGW(TAG, "Performing factory reset...");
      global_preferences->reset();

      App.scheduler.set_timeout(this, "reboot_after_reset", 300, []() {
        App.safe_reboot();
      });
    });
  }
}

// Get the status sent from device
void MideaDehumComponent::parseState() {
  bool updated = false;

  // --- Parse core operating parameters ---
  bool new_power = (serialRxBuf[11] & 0x01) != 0;
  uint8_t new_mode = serialRxBuf[12] & 0x0F;
  uint8_t new_fan = serialRxBuf[13] & 0x7F;
  uint8_t new_humidity_set = (serialRxBuf[17] > 100) ? 99 : serialRxBuf[17];

  // --- Environmental readings ---
  uint8_t new_humidity = serialRxBuf[26];
  float temp = (static_cast<int>(serialRxBuf[27]) - 50) / 2.0f;
  if (temp < -19.0f) temp = -20.0f;
  if (temp > 50.0f)  temp = 50.0f;
  float temperature_decimal = (serialRxBuf[28] & 0x0F) * 0.1f;
  if (temp >= 0.0f) temp += temperature_decimal; else temp -= temperature_decimal;
  float new_temp = temp;
  uint8_t new_error = serialRxBuf[31];
  
  // --- Compare and update core state fields ---
  if (new_power != this->state_.powerOn) { this->state_.powerOn = new_power; updated = true; }
  if (new_mode != this->state_.mode) { this->state_.mode = new_mode; updated = true; }
  if (new_fan != this->state_.fanSpeed) { this->state_.fanSpeed = new_fan; updated = true; }
  if (new_humidity_set != this->state_.humiditySetpoint) { this->state_.humiditySetpoint = new_humidity_set; updated = true; }
  if (new_humidity != this->state_.currentHumidity) { this->state_.currentHumidity = new_humidity; updated = true; }
  if (fabs(new_temp - this->state_.currentTemperature) > 0.1f) { this->state_.currentTemperature = new_temp; updated = true; }
  if (new_error != this->error_state_) { this->error_state_ = new_error; updated = true; }

  if (updated || first_run) {
    this->sendClimateState();
  }

#if defined(USE_MIDEA_DEHUM_ERROR) || defined(USE_MIDEA_DEHUM_BUCKET)
    if (first_run || this->error_state_ != new_error) {
      this->error_state_ = new_error;
#ifdef USE_MIDEA_DEHUM_ERROR
      if(this->error_sensor_) {this->error_sensor_->publish_state(this->error_state_);}
#endif
    }
#endif

#ifdef USE_MIDEA_DEHUM_BUCKET
    bool bucket_full = (this->error_state_ == 38);
    if (first_run || bucket_full != this->bucket_full_state_) {
      this->bucket_full_state_ = bucket_full;
      if (this->bucket_full_sensor_) this->bucket_full_sensor_->publish_state(bucket_full);
    }
#endif

#ifdef USE_MIDEA_DEHUM_TIMER
  // --- Parse timer fields from payload bytes 14..16 ---
  const uint8_t on_raw  = serialRxBuf[14];
  const uint8_t off_raw = serialRxBuf[15];
  const uint8_t ext_raw = serialRxBuf[16];

  const bool on_timer_set  = (on_raw  & 0x80) != 0;
  const bool off_timer_set = (off_raw & 0x80) != 0;

  uint8_t on_hr = 0, off_hr = 0;
  int on_min = 0, off_min = 0;

  if (on_timer_set) {
    on_hr  = (on_raw & 0x7C) >> 2;
    on_min = ((on_raw & 0x03) + 1) * 15 - ((ext_raw & 0xF0) >> 4);
    if (on_min < 0) on_min += 60;
  }

  if (off_timer_set) {
    off_hr  = (off_raw & 0x7C) >> 2;
    off_min = ((off_raw & 0x03) + 1) * 15 - (ext_raw & 0x0F);
    if (off_min < 0) off_min += 60;
  }

  const float on_timer_hours  = on_timer_set  ? (on_hr  + (on_min  / 60.0f)) : 0.0f;
  const float off_timer_hours = off_timer_set ? (off_hr + (off_min / 60.0f)) : 0.0f;

  // Cache raw bytes
  this->last_on_raw_  = on_raw;
  this->last_off_raw_ = off_raw;
  this->last_ext_raw_ = ext_raw;

  // Update HA entity with the *active* timer
  float timer_hours = 0.0f;
  if (!this->state_.powerOn && on_timer_set) {
    timer_hours = on_timer_hours;
  } else if (this->state_.powerOn && off_timer_set) {
    timer_hours = off_timer_hours;
  }

  if (this->timer_number_) {
    static float last_timer_hours = -1.0f;  // invalid initial value
    if (first_run || fabs(timer_hours - last_timer_hours) > 0.01f) {
      this->set_timer_hours(timer_hours, true);
      last_timer_hours = timer_hours;
    }
  }
#endif

// --- BYTE19 Related features ---
  
  // --- Panel light / brightness class (bits 7–6) ---
#ifdef USE_MIDEA_DEHUM_LIGHT
  uint8_t new_light_class = (serialRxBuf[19] & 0xC0) >> 6;
  if (new_light_class != this->light_class_ || first_run) {
    this->light_class_ = new_light_class;
    if (this->light_select_) {
      const char* light_str =
        new_light_class == 0 ? "Auto" :
        new_light_class == 1 ? "Off" :
        new_light_class == 2 ? "Low" : "High";
      this->light_select_->publish_state(light_str);
    }
  }
#endif

  // --- Ionizer (bit 6) ---
#ifdef USE_MIDEA_DEHUM_ION
  bool new_ion_state = (serialRxBuf[19] & 0x40) != 0;
  if (new_ion_state != this->ion_state_ || first_run) {
    if(this->state_.powerOn) {
      this->ion_state_ = new_ion_state;
      if (this->ion_switch_) this->ion_switch_->publish_state(new_ion_state);
    }
  }
#endif

  // --- Sleep mode (bit 5) ---
#ifdef USE_MIDEA_DEHUM_SLEEP
  bool new_sleep_state = (serialRxBuf[19] & 0x20) != 0;
  if (new_sleep_state != this->sleep_state_  || first_run) {
    if(this->state_.powerOn) {
      this->sleep_state_ = new_sleep_state;
      if (this->sleep_switch_) this->sleep_switch_->publish_state(new_sleep_state);
    }
  }
#endif

  // --- Optional: Pump bits (3–4) ---
#ifdef USE_MIDEA_DEHUM_PUMP
  bool new_pump_state = (serialRxBuf[19] & 0x08) != 0;
  if (new_pump_state != this->pump_state_  || first_run) {
    if(this->state_.powerOn) {
      this->pump_state_ = new_pump_state;
      if (this->pump_switch_) this->pump_switch_->publish_state(new_pump_state);
    }
  }  
#endif

  // --- Filter cleaning bit (7) ---
#ifdef USE_MIDEA_DEHUM_FILTER
  bool new_filter_request = (serialRxBuf[19] & 0x80) >> 7;
  if (new_filter_request != this->filter_request_state_  || first_run) {
    this->filter_request_state_ = new_filter_request;
    if (this->filter_request_sensor_) {
      this->filter_request_sensor_->publish_state(new_filter_request);
    }
  }
#endif

  // --- Tank / Water Level (Byte 20, bit 0-6) ---
#ifdef USE_MIDEA_DEHUM_TANK_LEVEL
  uint8_t tank_byte = serialRxBuf[20];
  uint8_t new_tank_level = tank_byte & 0x7F;

  if (new_tank_level != this->tank_level_  || first_run) {
    this->tank_level_ = new_tank_level;
    if (this->tank_level_sensor_)
      this->tank_level_sensor_->publish_state(new_tank_level);
  }
#endif

  // --- Defrosting (Byte 20, bit 7) ---
#ifdef USE_MIDEA_DEHUM_DEFROST
  bool new_defrost = (serialRxBuf[20] & 0x80) != 0;

  if (new_defrost != this->defrost_state_ || first_run) {
    this->defrost_state_ = new_defrost;
    if (this->defrost_sensor_)
      this->defrost_sensor_->publish_state(new_defrost);
  }
#endif

  // --- PM2.5 value (bytes 23–24) ---
#ifdef USE_MIDEA_DEHUM_PM25
  uint16_t new_pm25_value = static_cast<uint16_t>(serialRxBuf[23]) |
                        (static_cast<uint16_t>(serialRxBuf[24]) << 8);
  if (new_pm25_value != this->pm25_ || first_run) {
    this->pm25_ = new_pm25_value;
    if (this->pm25_sensor_) {
      this->pm25_sensor_->publish_state(new_pm25_value);
    }
  }
#endif

  // --- Horizontal swing (byte 29, bit 4) ---
#ifdef USE_MIDEA_DEHUM_HORIZONTAL_SWING
  bool new_horizontal_swing_state = (serialRxBuf[29] & 0x10) != 0;
  if (new_horizontal_swing_state != this->horizontal_swing_state_ || first_run) { 
    if(this->state_.powerOn) {
      this->horizontal_swing_state_ = new_horizontal_swing_state;
      this->publish_state();
    }
  }
#endif

  // --- Vertical swing (byte 29, bit 5) ---
#ifdef USE_MIDEA_DEHUM_SWING
  bool new_swing_state = (serialRxBuf[29] & 0x20) != 0;
  if (new_swing_state != this->swing_state_ || first_run) { 
    if(this->state_.powerOn) {
      this->swing_state_ = new_swing_state;
      this->publish_state();
    }
  }
#endif

  this->clearRxBuf();
  first_run = false;
}

climate::ClimateTraits MideaDehumComponent::traits() {
  climate::ClimateTraits t;
  t.add_feature_flags(
      climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
      climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY |
      climate::CLIMATE_SUPPORTS_TARGET_HUMIDITY
  );

  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);

  t.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);

#if defined(USE_MIDEA_DEHUM_SWING) || defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  climate::ClimateSwingModeMask swing_modes;

  swing_modes.insert(climate::CLIMATE_SWING_OFF);

#if defined(USE_MIDEA_DEHUM_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_VERTICAL);
#endif

#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_HORIZONTAL);
#endif

#if defined(USE_MIDEA_DEHUM_SWING) && defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  swing_modes.insert(climate::CLIMATE_SWING_BOTH);
#endif
  t.set_supported_swing_modes(swing_modes);
#endif

  t.set_visual_min_humidity(30.0f);
  t.set_visual_max_humidity(80.0f);
  
  std::vector<const char *> custom_presets;
  if (display_mode_setpoint_ != "UNUSED") custom_presets.push_back(display_mode_setpoint_.c_str());
  if (display_mode_continuous_ != "UNUSED") custom_presets.push_back(display_mode_continuous_.c_str());
  if (display_mode_smart_ != "UNUSED") custom_presets.push_back(display_mode_smart_.c_str());
  if (display_mode_clothes_drying_ != "UNUSED") custom_presets.push_back(display_mode_clothes_drying_.c_str());

  if (!custom_presets.empty()) {
    t.set_supported_custom_presets(custom_presets);
  }
  return t;
}

void MideaDehumComponent::handleStateUpdateRequest(std::string requestedState, uint8_t mode, uint8_t fanSpeed, uint8_t humiditySetpoint) {
  DehumidifierState newState = this->state_;

  if (requestedState == "on") newState.powerOn = true;
  else if (requestedState == "off") newState.powerOn = false;

  if (mode < 1 || mode > 4) mode = 3;
  newState.mode = mode;
  newState.fanSpeed = fanSpeed;

  if (humiditySetpoint && humiditySetpoint >= 35 && humiditySetpoint <= 85)
    newState.humiditySetpoint = humiditySetpoint;

  if (newState.powerOn != this->state_.powerOn ||
      newState.mode != this->state_.mode ||
      newState.fanSpeed != this->state_.fanSpeed ||
      newState.humiditySetpoint != this->state_.humiditySetpoint) {

    this->state_ = newState;
    this->sendSetStatus();
    this->sendClimateState();
  }
}

void MideaDehumComponent::sendSetStatus() {
  memset(setStatusCommand, 0, sizeof(setStatusCommand));

  // --- Command header ---
  setStatusCommand[0] = 0x48;  // Write command marker

  // --- Power and beep (byte 1) ---
  setStatusCommand[1] = this->state_.powerOn ? 0x01 : 0x00;
#ifdef USE_MIDEA_DEHUM_BEEP
  if (this->beep_state_) setStatusCommand[1] |= 0x40;  // bit6 = beep prompt
#endif

  // --- Mode (byte 2) ---
  uint8_t mode = this->state_.mode;
  if (mode < 1 || mode > 4) mode = 3;
  setStatusCommand[2] = mode & 0x0F;

  // --- Fan speed (byte 3) ---
  setStatusCommand[3] = (uint8_t)this->state_.fanSpeed;

#ifdef USE_MIDEA_DEHUM_TIMER
  uint8_t on_raw  = this->last_on_raw_;
  uint8_t off_raw = this->last_off_raw_;
  uint8_t ext_raw = this->last_ext_raw_;

  bool force_timer_apply = false;

  if (this->timer_write_pending_) {
    force_timer_apply = true;

    if (this->pending_timer_hours_ <= 0.01f) {
      on_raw = off_raw = ext_raw = 0x00;
    } else {
      uint16_t total_minutes = static_cast<uint16_t>(this->pending_timer_hours_ * 60.0f + 0.5f);
      uint8_t hours   = total_minutes / 60;
      uint8_t minutes = total_minutes % 60;

      if (minutes == 0 && hours > 0) { minutes = 60; hours--; }

      uint8_t minutesH = minutes / 15;
      uint8_t minutesL = 15 - (minutes % 15);
      if (minutes % 15 == 0) { minutesL = 0; if (minutesH > 0) minutesH--; }

      if (this->pending_applies_to_on_) {
        on_raw  = 0x80 | ((hours & 0x1F) << 2) | (minutesH & 0x03);
        ext_raw = (minutesL & 0x0F) << 4;
        off_raw = 0x00;
      } else {
        off_raw = 0x80 | ((hours & 0x1F) << 2) | (minutesH & 0x03);
        ext_raw = (minutesL & 0x0F);
        on_raw  = 0x00;
      }
    }

    this->last_on_raw_  = on_raw;
    this->last_off_raw_ = off_raw;
    this->last_ext_raw_ = ext_raw;
    this->timer_write_pending_ = false;
  }

  setStatusCommand[4] = on_raw;
  setStatusCommand[5] = off_raw;
  setStatusCommand[6] = ext_raw;

  if (force_timer_apply || (on_raw & 0x80) || (off_raw & 0x80)) {
    setStatusCommand[3] |= 0x80;
#ifdef USE_MIDEA_DEHUM_TIMERMODE_HINT
    setStatusCommand[1] |= 0x10;
#endif
  } else {
    setStatusCommand[3] &= static_cast<uint8_t>(~0x80);
  }

#endif

  // --- Target humidity (byte 7) ---
  setStatusCommand[7] = this->state_.humiditySetpoint;

  // --- Misc feature flags (byte 9) ---
  uint8_t b9 = 0;

#ifdef USE_MIDEA_DEHUM_LIGHT
  // bits 7–6 = panel light brightness mode (0–3)
  b9 |= (this->light_class_ & 0x03) << 6;
#endif
#ifdef USE_MIDEA_DEHUM_ION
  if (this->ion_state_) b9 |= 0x40;  // bit6 = ionizer on
#endif
#ifdef USE_MIDEA_DEHUM_SLEEP
  if (this->sleep_state_) b9 |= 0x20;  // bit5 = sleep
#endif
#ifdef USE_MIDEA_DEHUM_PUMP
  if (this->pump_state_) {
    b9 |= 0x18;  // bit4 (flag) + bit3 (on)
  } else {
    b9 |= 0x10;  // bit4 = pump control active, bit3 = off
  }
#endif
#ifdef USE_MIDEA_DEHUM_FILTER_BUTTON
  // --- Include "filter cleaned" flag if requested ---
  if (this->filter_cleaned_flag_) {
    b9 |= 0x80;
    this->filter_cleaned_flag_ = false;
  }
#endif

  setStatusCommand[9] = b9;

  // --- Swing (byte 20, bit5) ---
#ifdef USE_MIDEA_DEHUM_SWING
  uint8_t swing_flags   = 0x00;
  if (this->swing_state_) swing_flags |= 0x08;
  setStatusCommand[10] = swing_flags;
#endif

  // --- Send assembled frame ---
  this->sendMessage(0x02, 0x03, 0x00, 25, setStatusCommand);
}

void MideaDehumComponent::sendClimateState(){
      // Compute climate mode
    this->mode = this->state_.powerOn ? climate::CLIMATE_MODE_DRY : climate::CLIMATE_MODE_OFF;

    // Fan level mapping
    if (this->state_.fanSpeed <= 50)
      this->fan_mode = climate::CLIMATE_FAN_LOW;
    else if (this->state_.fanSpeed <= 70)
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
    else
      this->fan_mode = climate::CLIMATE_FAN_HIGH;

    // Determine mode preset
    switch (this->state_.mode) {
      case 1:
        this->set_custom_preset_(display_mode_setpoint_.c_str());
        break;

      case 2:
        this->set_custom_preset_(display_mode_continuous_.c_str());
        break;

      case 3:
        this->set_custom_preset_(display_mode_smart_.c_str());
        break;

      case 4:
        this->set_custom_preset_(display_mode_clothes_drying_.c_str());
        break;

      default:
        this->set_custom_preset_(display_mode_smart_.c_str());
        break;
    }

    this->target_humidity = int(this->state_.humiditySetpoint);
    this->current_humidity = int(this->state_.currentHumidity);
    this->current_temperature = this->state_.currentTemperature;

    this->publish_state();  // Update main HA entity
}

void MideaDehumComponent::updateAndSendNetworkStatus(bool connected) {
  memset(networkStatus, 0, sizeof(networkStatus));
  if(connected) {
    // Byte 0: module type (Wi-Fi)
    networkStatus[0] = 0x01;

    // Byte 1: Wi-Fi mode
    networkStatus[1] = 0x01;

    networkStatus[2] = 0x04;

    networkStatus[3] = 0x01;
    networkStatus[4] = 0x00;
    networkStatus[5] = 0x00;
    networkStatus[6] = 0x7F;

    // Byte 7: RF signal (not used)
    networkStatus[7] = 0xFF;

    // Byte 8: router status
    networkStatus[8] = 0x00;

    // Byte 9: cloud
    networkStatus[9] = 0x00;

    // Byte 10: Direct LAN connection (not applicable)
    networkStatus[10] = 0x00;

    // Byte 11: TCP connection count (not used)
    networkStatus[11] = 0x00;

    networkStatus[12] = 0x01;
  }
  else {
    networkStatus[0] = 0x01;
    networkStatus[1] = 0x01;
    networkStatus[7] = 0xFF;
    networkStatus[8] = 0x02;
  }

  this->sendMessage(0x0D, 0x03, 0xBF, sizeof(networkStatus), networkStatus);
}

void MideaDehumComponent::getStatus() {
  this->sendMessage(0x03, 0x03, 0x00, 21, getStatusCommand);
}

void MideaDehumComponent::sendMessage(uint8_t msgType, uint8_t agreementVersion, uint8_t frameSyncCheck, uint8_t payloadLength, uint8_t *payload) {
  this->clearTxBuf();
  this->writeHeader(msgType, agreementVersion, frameSyncCheck, payloadLength);
  memcpy(serialTxBuf, currentHeader, 10);
  memcpy(serialTxBuf + 10, payload, payloadLength);
  serialTxBuf[10 + payloadLength]     = crc8(serialTxBuf + 10, payloadLength);
  serialTxBuf[10 + payloadLength + 1] = checksum(serialTxBuf, 10 + payloadLength + 1);

  const size_t total_len = 10 + payloadLength + 2;

  this->write_array(serialTxBuf, total_len);
}

// ===== Climate control =======================================================
void MideaDehumComponent::control(const climate::ClimateCall &call) {
  std::string requestedState = this->state_.powerOn ? "on" : "off";
  uint8_t reqMode = this->state_.mode;
  uint8_t reqFan = this->state_.fanSpeed;
  uint8_t reqSet = this->state_.humiditySetpoint;

  if (call.get_mode().has_value())
    requestedState = *call.get_mode() == climate::CLIMATE_MODE_OFF ? "off" : "on";
 
  if (const char *preset = call.get_custom_preset()) {
      std::string requestedPreset(preset);

      if (requestedPreset == display_mode_setpoint_)
        reqMode = 1;
      else if (requestedPreset == display_mode_continuous_)
        reqMode = 2;
      else if (requestedPreset == display_mode_smart_)
        reqMode = 3;
      else if (requestedPreset == display_mode_clothes_drying_)
        reqMode = 4;
      else
        reqMode = 3;  // default fallback
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_LOW:    reqFan = 40; break;
      case climate::CLIMATE_FAN_MEDIUM: reqFan = 60; break;
      case climate::CLIMATE_FAN_HIGH:   reqFan = 80; break;
      default:                          reqFan = 60; break;
    }
  }

if (call.get_target_humidity().has_value()) {
  float h = *call.get_target_humidity();
  if (h >= 30.0f && h <= 99.0f)
    reqSet = (uint8_t) std::round(h);
}

#if defined(USE_MIDEA_DEHUM_SWING) || defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
  if (call.get_swing_mode().has_value()) {

#if defined(USE_MIDEA_DEHUM_SWING)
    bool old_vertical = this->swing_state_;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
    bool old_horizontal = this->horizontal_swing_state_;
#endif

    switch (*call.get_swing_mode()) {
      case climate::CLIMATE_SWING_OFF:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = false;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = false;
#endif
        break;

      case climate::CLIMATE_SWING_VERTICAL:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = true;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = false;
#endif
        break;

      case climate::CLIMATE_SWING_HORIZONTAL:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = false;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = true;
#endif
        break;

      case climate::CLIMATE_SWING_BOTH:
#if defined(USE_MIDEA_DEHUM_SWING)
        this->swing_state_ = true;
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
        this->horizontal_swing_state_ = true;
#endif
        break;

      default:
        break;
    }

    bool changed = false;

#if defined(USE_MIDEA_DEHUM_SWING)
    changed |= (this->swing_state_ != old_vertical);
#endif
#if defined(USE_MIDEA_DEHUM_HORIZONTAL_SWING)
    changed |= (this->horizontal_swing_state_ != old_horizontal);
#endif

    if (changed) {
      this->sendSetStatus();
      this->publish_state();
    }
  }
#endif

  this->handleStateUpdateRequest(requestedState, reqMode, reqFan, reqSet);
}

}  // namespace midea_dehum
}  // namespace esphome
