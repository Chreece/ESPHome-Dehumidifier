<div align="center">
  <img src="https://github.com/Hypfer/esp8266-midea-dehumidifier/blob/master/img/logo.svg" width="800" alt="esp8266-midea-dehumidifier">
  <h2>Free your dehumidifier from the cloud — now with ESPHome</h2>
</div>
# Update 21/10/2025, in this release:

* 🆕 Added internal device timer support (optional)

* 🤝 Improved handshake reliability

* ⚙️ General background and stability improvements


This project is an **ESPHome-based port** of [Hypfer’s esp8266-midea-dehumidifier](https://github.com/Hypfer/esp8266-midea-dehumidifier).  
While the original version used a custom MQTT firmware, this one is a **native ESPHome component**, providing full **Home Assistant integration** without MQTT or cloud dependencies.

Example entities for Inventor EVA II pro:
<div align="center">
  <img width="612" height="185" alt="image" src="https://github.com/user-attachments/assets/467446b5-e728-4d70-9080-546aabac71a2" />
  <img width="686" height="785" alt="image" src="https://github.com/user-attachments/assets/96847d77-a75a-478b-9705-b8e2e4dae7be" />
  <img width="660" height="767" alt="image" src="https://github.com/user-attachments/assets/23494942-0892-4933-8bc5-1a09bd2b63a4" />
</div>
---

## ✨ Features

This component allows you to directly control and monitor Midea-based dehumidifiers via UART, completely bypassing the Midea cloud dongle.

Supported entities:

| Entity Type     | Description |
|------------------|-------------|
| **Climate**      | Power, mode, fan speed, and presets |
| **Binary Sensor (optional)** | "Bucket Full" indicator |
| **Error Sensor (optional)** | Reports current error code (optional in YAML) |
| **ION Switch (optional)** | Controls ionizer state if supported |
| **Swing Switch (optional)** | Controls swing if supported |
| **Timer Number (optional)** | Controls the internal device timer if supported |

Optional entities can be included or excluded simply by adding or omitting them from your YAML.

---

## 🧠 Background

Midea-made dehumidifiers (sold under brands like *Inventor*, *Comfee*, *Midea*, etc.) use a UART-based protocol behind their “WiFi SmartKey” dongles.

Those dongles wrap simple serial communication in cloud encryption and authentication layers.  
By connecting directly to the UART pins inside the unit, you can fully control it locally — no cloud, no reverse proxy, no token handshakes.

---

## 🧩 Compatibility

These models are confirmed to work (and more likely will, too):

* Midea MAD22S1WWT  
* Comfee MDDF-16DEN7-WF  
* Comfee MDDF-20DEN7-WF
* Comfee CDDF7-16DEN7-WFI
* Inventor Eva II PRO Wi-Fi  
* Inventor EVA ION PRO Wi-Fi 20L  
* Midea Cube 20 / 35 / 50  

Models without USB or Wi-Fi button (e.g., Comfee MDDF-20DEN7) could also work with small wiring changes.

---

## 🧰 Hardware Setup

You’ll need:

* **ESP32** (or ESP8266) board
  
* **UART connection** (TX/RX) to your dehumidifier’s USB A female adapter (i.e. male USB A adapter with pins for connection see following photo)
 
![17605125491072937899494889157102](https://github.com/user-attachments/assets/166900a0-045f-42d4-80bc-405f7af4ed5c)

* **3.3 V ↔ 5 V level shifting** (if necessary)

The Midea WiFi dongle is just a UART-to-cloud bridge — unplug it and connect your ESP board instead:

| Dongle Pin | Function | ESP Pin Example |
|-------------|-----------|----------------|
| 1 | 5 V | VIN |
| 2 | TX | GPIO17 |
| 3 | RX | GPIO16 |
| 4 | GND | GND |

---

## ⚙️ ESPHome Configuration

Example YAML:

```yaml
esphome:
  name: midea-dehumidifier

esp32:
  board: esp32dev
  framework:
    type: esp-idf

external_components:
  - source:
      type: git
      url: https://github.com/Chreece/ESPHome-Dehumidifier
      ref: main
    components: [midea_dehum]
    refresh: 0min

uart:
  id: uart_midea
  tx_pin: GPIO16
  rx_pin: GPIO17
  baud_rate: 9600

midea_dehum:
  id: midea_dehum_comp
  uart_id: uart_midea

  status_poll_interval: 30000 # Optional, how often should ESP ask the device for a status update in ms (1000ms=1sec). Default: 30000ms

  # 🆕 Optional: Rename display modes to match your device’s front panel.
  # For example, your unit may label these as “Cont”, “Dry”, or “Smart”.
  # These names only affect how the presets appear in Home Assistant — 
  # the internal logic and protocol remain the same.

  # 💡 Tip:
  # If any of the modes below are set to "UNUSED" (case-insensitive),
  # that preset will NOT appear in the Home Assistant UI.
  # Use this if your device doesn’t support or respond to a specific mode.
  # For instance, if pressing “SMART”, your unit doesn't change any mode,
  # set display_mode_smart: "UNUSED" to hide it from the UI.

  display_mode_setpoint: 'UNUSED'      # Hidden in Home Assistant (disabled)
  display_mode_continuous: 'Cont'      # Shown as "Cont"
  display_mode_smart: 'Smart'          # Shown as "Smart"
  display_mode_clothes_drying: 'Dry'   # Shown as "Dry"

climate:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    name: "Inventor Dehumidifier"

binary_sensor:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    bucket_full:
      name: "Bucket Full"

# Optional error sensor remove this block if not needed
sensor:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    error:
      name: "Error Code"

switch:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
# Optional ionizer control, add this block only if your device has Ionizer
    ionizer:
      name: "Ionizer"
# 🆕 Optional swing control (if supported)
    swing:
      name: "Swing"

# Optional timer number entity for the internal device timer
# When device off -> timer to turn on
# When device on -> timer to turn off
# Toggling the device on/off resets the timer
# 0.5h increments, max: 24h
number:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    timer:
      name: "Internal Device Timer"

```
All entities appear automatically in Home Assistant with native ESPHome support.

🧩 Component Architecture

File	Purpose
midea_dehum.cpp/h	Core UART communication and protocol handling
climate.py	Main control entity (mode, fan, humidity, etc.)
binary_sensor.py	“Bucket full” status
sensor.py	Optional error code reporting
switch.py	Optional switches
number.py Optional timer entity

🧪 Supported Features

Power on/off

Mode control (Setpoint, Continuous, Smart, ClothesDrying, etc.)

Fan speed control

Humidity Control Target & Current humidity (via native ESPHome climate interface)

Current Temperature (integer)

Swing Control	Toggle air swing direction (if supported by device)

Bucket full status

Error code reporting (optional)

Ionizer toggle (if supported)

On/Off timer (if supported)

Note: The Temperature-Humidity values from device aren't reliable, better not use them for automations.

🆕 Renamable operating mode labels to match your dehumidifier’s printed icons

⚠️ Safety Notice

Many of these dehumidifiers use R290 (Propane) as refrigerant.
This gas is flammable. Be extremely careful when opening or modifying your unit.
Avoid sparks, heat, or metal contact that could pierce the sealed system.

🧱 Development Notes (Updated)

Fully implements ESPHome’s native climate humidity support, exposing both current and target humidity.

Added temperature reporting (integer precision).

Added swing control switch for devices that support oscillation control.

Modular design — optional parts (Ionizer, Swing, Error sensor) are compiled only if configured.

⚠️ Disclaimer

This project interacts directly with hardware inside a mains-powered appliance that may use R290 (propane) refrigerant.
Modifying or opening such devices can be dangerous and may cause electric shock, fire, or injury if not done safely.

By using this project, you agree that:

You perform all modifications at your own risk.

The author(s) and contributors are not responsible for any damage, data loss, or injury.

Always disconnect power before working on the device.

Never operate the unit open or modified near flammable materials.

If you’re not confident working with electrical components, don’t attempt this modification.

🧑‍💻 Credits


👉 [Hypfer/esp8266-midea-dehumidifier](https://github.com/Hypfer/esp8266-midea-dehumidifier)

Swing control and native humidity integration contributed by [CDank](https://github.com/CDank) — huge thanks for the collaboration and implementation help!

It builds upon reverse-engineering efforts and research from:

[**Mac Zhou**](https://github.com/mac-zhou/midea-msmart)

[**NeoAcheron**](https://github.com/NeoAcheron/midea-ac-py)

[**Rene Klootwijk**](https://github.com/reneklootwijk/node-mideahvac)


📜 License

This port follows the same open-source spirit as the original project.
See [LICENSE](https://github.com/Chreece/ESPHome-Dehumidifier/blob/main/LICENSE) for details.

<div align="center"> <sub> Made with ❤️ by <a href="https://github.com/Chreece">Chreece</a> — This project is based on <a href="https://github.com/Hypfer/esp8266-midea-dehumidifier">Hypfer's esp8266-midea-dehumidifier</a>, originally licensed under the Apache License 2.0.<br> Modifications and ESPHome integration © 2025 Chreece.<br> Original logo © Hypfer, used here for attribution under the Apache License 2.0. </sub> </div>
