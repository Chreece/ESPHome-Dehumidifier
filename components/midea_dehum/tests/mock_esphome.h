// Minimal ESPHome stubs for host-side unit testing.
// Provides just enough of the ESPHome API so midea_dehum_*.cpp can compile
// with a standard C++17 compiler (g++/clang++).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ── Logging ─────────────────────────────────────────────────────────────

#define ESP_LOGI(tag, fmt, ...) printf("[INFO  %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[WARN  %s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[ERROR %s] " fmt "\n", tag, ##__VA_ARGS__)

#define ESPHOME_VERSION_CODE VERSION_CODE(2026, 4, 0)
#define VERSION_CODE(major, minor, patch) ((major) << 16 | (minor) << 8 | (patch))

// ── Time ────────────────────────────────────────────────────────────────

inline uint32_t millis() {
  static uint32_t t = 0;
  return ++t;  // monotonic tick, 1ms per call — good enough for tests
}

// ── Scheduler ───────────────────────────────────────────────────────────

// Runs all queued callbacks immediately (no real delays for tests).
// Call run_scheduler() after each test step to drain pending timeouts.
struct SchedulerEntry {
  std::string name;
  uint32_t trigger_ms;
  std::function<void()> fn;
};

inline std::vector<SchedulerEntry>& scheduler_queue() {
  static std::vector<SchedulerEntry> q;
  return q;
}

inline void scheduler_set_timeout(const std::string& name, uint32_t delay_ms,
                                  std::function<void()> fn) {
  uint32_t now = millis();
  scheduler_queue().push_back({name, now + delay_ms, std::move(fn)});
}

inline void run_scheduler() {
  // Run pending timeouts, allowing new ones to be queued, up to a limit
  for (int iter = 0; iter < 20 && !scheduler_queue().empty(); iter++) {
    auto q = std::move(scheduler_queue());
    scheduler_queue().clear();
    for (auto& e : q) {
      e.fn();
    }
  }
}

// Clear all queued callbacks (call between tests to prevent
// stale captured pointers from previous test objects)
inline void reset_scheduler() {
  scheduler_queue().clear();
}

// Run exactly one pass of pending timeouts (useful when you need
// precise control, e.g. auto-detect tests that inject status between rounds)
inline void run_scheduler_once() {
  if (scheduler_queue().empty()) return;
  auto q = std::move(scheduler_queue());
  scheduler_queue().clear();
  for (auto& e : q) {
    e.fn();
  }
}

// ── Namespace shims ─────────────────────────────────────────────────────

namespace esphome {

// —— Core ——
class Component {
public:
  virtual void setup() {}
  virtual void loop() {}
  float get_setup_priority() const { return 0; }
};

namespace scheduler {
inline void set_timeout(Component*, const std::string& name, uint32_t delay_ms,
                        std::function<void()> fn) {
  scheduler_set_timeout(name, delay_ms, std::move(fn));
}
}  // namespace scheduler

inline void safe_reboot() {
  printf("[REBOOT]\n");
}

// —— Preferences ——
class ESPPreferences {
public:
  template <typename T>
  class Preference {
  public:
    bool save(const T*) { return true; }
    bool load(T*) { return false; }
  };
  template <typename T>
  Preference<T> make_preference(uint32_t) {
    return Preference<T>();
  }
  void reset() {}
};
inline ESPPreferences* global_preferences = new ESPPreferences();

// —— Application ——
struct Application {
  // Provides the scheduler that the component code references as App.scheduler
  struct Scheduler {
    template <typename... Args>
    void set_timeout(Component*, const std::string& name, uint32_t delay_ms, Args&&... args) {
      std::function<void()> fn(std::forward<Args>(args)...);
      scheduler_set_timeout(name, delay_ms, std::move(fn));
    }
  };
  Scheduler scheduler;
  void safe_reboot() { printf("[REBOOT]\n"); }
};
inline Application App;

// —— UART ——
namespace uart {

class UARTComponent {
public:
  virtual bool read_byte(uint8_t* out)                      = 0;
  virtual int available()                                   = 0;
  virtual void write_array(const uint8_t* data, size_t len) = 0;
};

class UARTDevice {
public:
  void set_uart_parent(UARTComponent* uart) { uart_parent_ = uart; }
  void write_array(const uint8_t* data, size_t len) {
    if (uart_parent_) uart_parent_->write_array(data, len);
  }

protected:
  UARTComponent* uart_parent_{nullptr};
};

}  // namespace uart

// —— Climate ——
namespace climate {

enum ClimateMode : uint8_t {
  CLIMATE_MODE_OFF = 0,
  CLIMATE_MODE_DRY = 3,
};

enum ClimateFanMode : uint8_t {
  CLIMATE_FAN_LOW    = 0,
  CLIMATE_FAN_MEDIUM = 1,
  CLIMATE_FAN_HIGH   = 2,
};

enum ClimateSwingMode : uint8_t {
  CLIMATE_SWING_OFF        = 0,
  CLIMATE_SWING_VERTICAL   = 1,
  CLIMATE_SWING_HORIZONTAL = 2,
  CLIMATE_SWING_BOTH       = 3,
};

struct ClimateSwingModeMask {
  std::vector<ClimateSwingMode> modes;
  void insert(ClimateSwingMode m) { modes.push_back(m); }
  auto begin() { return modes.begin(); }
  auto end() { return modes.end(); }
  auto size() const { return modes.size(); }
};

static constexpr uint32_t CLIMATE_SUPPORTS_CURRENT_TEMPERATURE = 1 << 0;
static constexpr uint32_t CLIMATE_SUPPORTS_CURRENT_HUMIDITY    = 1 << 1;
static constexpr uint32_t CLIMATE_SUPPORTS_TARGET_HUMIDITY     = 1 << 2;

struct ClimateCall {
  std::optional<ClimateMode> mode_;
  std::optional<ClimateFanMode> fan_mode_;
  std::optional<float> target_humidity_;
  std::optional<ClimateSwingMode> swing_mode_;
  std::string preset_;

  std::optional<ClimateMode> get_mode() const { return mode_; }
  std::optional<ClimateFanMode> get_fan_mode() const { return fan_mode_; }
  std::optional<float> get_target_humidity() const { return target_humidity_; }
  std::optional<ClimateSwingMode> get_swing_mode() const { return swing_mode_; }
  std::string get_custom_preset() const { return preset_; }
};

struct ClimateTraits {
  std::vector<ClimateMode> supported_modes_;
  std::vector<ClimateFanMode> fan_modes_;
  ClimateSwingModeMask swing_modes_;
  float visual_min_humidity_{30};
  float visual_max_humidity_{80};
  uint32_t feature_flags_{0};

  void add_feature_flags(uint32_t f) { feature_flags_ |= f; }
  void add_supported_mode(ClimateMode m) { supported_modes_.push_back(m); }
  void add_supported_fan_mode(ClimateFanMode f) { fan_modes_.push_back(f); }
  void set_supported_swing_modes(const ClimateSwingModeMask& m) { swing_modes_ = m; }
  void set_visual_min_humidity(float h) { visual_min_humidity_ = h; }
  void set_visual_max_humidity(float h) { visual_max_humidity_ = h; }
  void set_supported_custom_presets(const std::vector<const char*>&) {}
};

// Using std::string for StringRef (ESPHome uses a custom StringRef but std::string works)
class Climate {
public:
  ClimateMode mode{CLIMATE_MODE_OFF};
  ClimateFanMode fan_mode{CLIMATE_FAN_LOW};
  ClimateSwingMode swing_mode{CLIMATE_SWING_OFF};
  float current_temperature{0};
  float current_humidity{0};
  float target_humidity{50};
  std::string custom_preset_;
  const char* custom_preset_ptr_{nullptr};

  // These are called by the component code to publish state to HA
  void publish_state() {
    // no-op in tests — we read the fields directly
  }
  void set_custom_preset_(const char* preset) { custom_preset_ptr_ = preset; }

  void set_supported_custom_presets(const std::vector<const char*>&) {}

  virtual ClimateTraits traits()                = 0;
  virtual void control(const ClimateCall& call) = 0;
};

}  // namespace climate

using StringRef = std::string;

// —— Binary sensor ——
namespace binary_sensor {
class BinarySensor {
public:
  bool state{false};
  void publish_state(bool s) { state = s; }
};
}  // namespace binary_sensor

// —— Sensor ——
namespace sensor {
class Sensor {
public:
  float state{0};
  void publish_state(float s) { state = s; }
  void publish_state(uint8_t s) { state = static_cast<float>(s); }
};
}  // namespace sensor

// —— Switch ——
namespace switch_ {
class Switch {
public:
  bool state{false};
  virtual void write_state(bool s) { state = s; }
  void publish_state(bool s) { state = s; }
};
}  // namespace switch_

// —— Button ——
namespace button {
class Button {
public:
  virtual void press_action() = 0;
};
}  // namespace button

// —— Number ——
namespace number {
class Number {
public:
  float state{0};
  virtual void control(float value) { state = value; }
  void publish_state(float s) { state = s; }
};
}  // namespace number

// —— Text Sensor ——
namespace text_sensor {
class TextSensor {
public:
  std::string state;
  void publish_state(const std::string& s) { state = s; }
};
}  // namespace text_sensor

// —— Select ——
namespace select {
class Select {
public:
  std::string state;
  void publish_state(const char* s) { state = s; }
};
}  // namespace select

}  // namespace esphome
