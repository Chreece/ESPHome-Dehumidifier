<div align="center">
  <img src="https://github.com/Hypfer/esp8266-midea-dehumidifier/blob/master/img/logo.svg" width="800" alt="esp8266-midea-dehumidifier">
  <h2>Free your dehumidifier from the cloud — now with ESPHome</h2>
</div>

This project is an **ESPHome-based port** of [Hypfer’s esp8266-midea-dehumidifier](https://github.com/Hypfer/esp8266-midea-dehumidifier).
While the original version used a custom MQTT firmware, this one is a **native ESPHome component**, providing full **Home Assistant integration** without MQTT or cloud dependencies.
**Minimum ESPHome version: 2025.11**

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

| Entity Type | Description |
|-------------|-------------|
| **Climate** | Power, mode, fan speed, swing (vertical/horizontal) and presets |
| **Bucket Full Binary Sensor** (optional) | "Bucket Full" indicator |
| **Clean Filter Binary Sensor** (optional) | "Clean Filter" notification if supported |
| **Defrosting Binary Sensor** (optional) | Defrosting indicator if supported |
| **Error Sensor** (optional) | Reports current error code |
| **Tank Water Level Sensor** (optional) | Reports current tank water level |
| **Humidity Sensor** (optional) | Reports current ambient humidity (chartable in HA) |
| **Temperature Sensor** (optional) | Reports current ambient temperature (chartable in HA) |
| **PM2.5 Sensor** (optional) | Reports PM2.5 particles from sensor if supported |
| **ION Switch** (optional) | Controls ionizer state if supported |
| **Beep Switch** (optional) | Controls buzzer on HA commands if supported |
| **Sleep Switch** (optional) | Controls sleep mode if supported |
| **Pump Switch** (optional) | Controls pump if supported |
| **Timer Number** (optional) | Controls the internal device timer if supported |
| **Target Humidity Number** (optional) | Standalone humidity setpoint control |
| **Filter Cleaned Button** (optional) | Resets the filter clean notification |
| **Reset Water Level Button** (optional) | Resets the water-level runtime counter |
| **Capabilities Text Sensor** (optional) | Shows discovered device capabilities |
| **Protocol Text Sensor** (optional) | Shows active protocol version (V1/V2/auto) |

Optional entities can be included or excluded simply by adding or omitting them from your YAML.

---

## 🧠 Background

Midea-made dehumidifiers (sold under brands like *Inventor*, *Comfee*, *Midea*, etc.) use a UART-based protocol behind their “WiFi SmartKey” dongles.

Those dongles wrap simple serial communication in cloud encryption and authentication layers.
By connecting directly to the UART pins inside the unit, you can fully control it locally — no cloud, no reverse proxy, no token handshakes.

---

## 🧩 Compatibility

### Confirmed & Expected Models

If unsure, use `protocol_version: 0` (auto-detect) — it tries both protocols at boot.
Models marked `?` need verification but are expected to work.

| Brand | Name | Model | Protocol |
|-------|------|-------|----------|
| Midea | Cube 20 Pint | MAD20S1QWT | v1 |
| Midea | Cube 35 Pint | MAD35S1QWT | v1 |
| Midea | Cube 50 Pint | MAD50S1QWT | ? |
| Midea | Cube 50 Pint with Pump | MAD50PS1QWT | ? |
| Midea | Cube 50 Pint with Pump | MAD50PS1QWT-A | v2 |
| Midea | Cube 50 Pint with Pump | MAD50PS1QWT-B | ? |
| Midea | Cube 50 Pint with Pump | MAD50PS1QWT-S | ? |
| Midea | Cube 50 Pint with Pump (GR) | MAD50PS1QGR | ? |
| Midea | Dehumidifier | MAD22S1WWT | v1 |
| Midea | Dehumidifier | MAD50C1AWS | v1 |
| Comfee | Dehumidifier | MDDF-16DEN7-WF | v1 |
| Comfee | Dehumidifier | MDDF-20DEN7-WF | v1 |
| Comfee | Dehumidifier | CDDF7-16DEN7-WFI | v1 |
| Inventor | Eva II PRO Wi-Fi |  | v1 |
| Inventor | EVA ION PRO Wi-Fi 20L |  | v1 |
| Emelson | Dehumidifier | EMLDH20DFR29 | v1 |

**Protocol versions:**
- **0 — auto-detect (default)** — Alternates V1/V2 every 1s until the MCU
  responds, then locks in the matching protocol. Gives up after 2 minutes if
  no response. Works with any model.
- **1** — Original implementation. Works with most Midea-based dehumidifiers.
- **2** — Based off of the stock RTL8720 dongle (MAD50PS1QWT-A verified).

Only the selected version(s) are compiled into firmware, keeping flash usage minimal.
Pin the protocol with `protocol_version: 1` or `protocol_version: 2` if you know
your device — it skips the auto-detect window and speeds up boot.

Models without USB or Wi-Fi button (e.g., Comfee MDDF-20DEN7, Emelson EMLDH20DFR29) could also work with small wiring changes.

For the full V2 wire protocol specification (frame layouts, handshake sequence, status byte map), see [`protocol_v2.md`](components/midea_dehum/protocol_v2.md).

---

## 🧰 Hardware Setup

You’ll need:

* **ESP32** (or ESP8266) board
* **UART connection** (TX/RX) to your dehumidifier's USB-A port (e.g. a male USB-A adapter with exposed pins):

![17605125491072937899494889157102](https://github.com/user-attachments/assets/166900a0-045f-42d4-80bc-405f7af4ed5c)

* **3.3V ↔ 5V level shifting** (recommended — the MAD50PS1QWT-A MCU uses 5V logic; other models may differ)

The Midea WiFi dongle is just a UART-to-cloud bridge — unplug it and connect your ESP board instead.

### USB-A Port Pinout

The dehumidifier's USB-A port carries UART signals (not USB data) at 9600 baud. **The D+/D- naming is the opposite of what you'd expect:**

| USB-A Pin | Signal | Direction | Connect to ESP |
|-----------|--------|-----------|----------------|
| 1 | GND | — | GND |
| 2 | D- | MCU TX | ESP RX |
| 3 | D+ | MCU RX | ESP TX |
| 4 | 5V | Power | VIN / 5V |

> **⚠ D+/D- are crossed:** Pin 2 (D-) is the MCU's **TX** line, and Pin 3 (D+) is the MCU's **RX** line. Connect MCU TX → ESP RX and MCU RX → ESP TX. If you get no response from the MCU, swap the two data wires.

### Build Options

There are two common approaches depending on your board and comfort level:

- **Direct USB-A adapter** — Wire TX/RX/GND straight from a USB-A breakout to the ESP. Simplest, no level shifter. Works for many users but runs 3.3V GPIO at 5V signals (outside spec).
- **Level-shifted build** — Uses a BSS138 level shifter for proper 3.3V↔5V translation. Safe and reliable, recommended for new builds. Example wiring included for the XIAO ESP32C6.

For full wiring diagrams, parts lists, and troubleshooting, see **[HARDWARE.md](HARDWARE.md)**.

---

## ⚙️ ESPHome Configuration

Example YAML with all supported sensors - controls, full example in [dehumidifier.yaml](https://raw.githubusercontent.com/Chreece/ESPHome-Dehumidifier/refs/heads/main/dehumidifier.yaml):

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Chreece/ESPHome-Dehumidifier
      ref: main
    components: [midea_dehum]
    refresh: 0min

uart:
  id: uart_midea
  tx_pin: GPIO16 # replace with the TX pin used from esp
  rx_pin: GPIO17 # replace with the RX pin used from esp
  baud_rate: 9600

midea_dehum:
  id: midea_dehum_comp
  uart_id: uart_midea
  # protocol_version: 0  # 0=auto-detect (default), 1=V1, 2=V2 — pin if known to speed up boot
  handshake_enabled: false # Optional if you have problems with unknown states on esp boot
  status_poll_interval: 1000 # Optional, how often should get a status update in ms (1000ms=1sec). Default: 1000ms

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
  display_mode_setpoint: 'UNUSED'
  display_mode_continuous: 'Cont'
  display_mode_smart: 'Smart'
  display_mode_clothes_drying: 'Dry'

climate:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    name: "Inventor Dehumidifier"
# Optional vertical swing control (if supported)
    swing: true
# Optional horizontal swing control (if supported)
    horizontal_swing: true

binary_sensor:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
# Optional sensor to inform when the Bucket is full
    bucket_full:
      name: "Bucket Full"
# Optional sensor to inform that a filter cleaning is required (only if supported)
    clean_filter:
      name: "Clean Filter Request"
# Optional sensor to inform if the defrosting procedure running (only if supported)
    defrost:
      name: "Defrosting"

button:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
# Optional button to reset the filter clean binary_sensor
    filter_cleaned:
      name: "Reset Filter Cleaning"
# Optional button to reset the water-level runtime counter (V2 only)
    reset_water_level:
      name: "Reset Water Level"

# Optional error sensor remove this block if not needed
sensor:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    error:
      name: "Error Code"
# Optional tank water level sensor (if supported)
    tank_level:
      name: "Tank water level"
# Optional current humidity sensor (chartable in Home Assistant)
    humidity:
      name: "Current Humidity"
# Optional current temperature sensor (chartable in Home Assistant)
    temperature:
      name: "Current Temperature"
# Optional pm2.5 sensor (if supported)
    pm25:
      name: "pm2.5"

# Optional switches
switch:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
# Optional ionizer control, add this block only if your device has Ionizer
    ionizer:
      name: "Ionizer"
# Optional control the device pump (if supported)
    pump:
      name: 'Pump'
# Optional sleep mode toggle (not all models support this)
# Enables or disables “Sleep” mode if available on your device (not tested!).
    sleep:
      name: "Sleep Mode"
# Optional beep control
# When enabled, the device will emit a beep sound when it receives
# commands (e.g. from Home Assistant or OTA updates).
    beep:
      name: "Beep on Command"

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
# Optional standalone humidity setpoint control (35-85%)
    target_humidity:
      name: "Target Humidity"

# Optional text sensors
text_sensor:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
# Optional text sensor to show discovered device capabilities
# Useful for diagnostics — helps confirm which features your model supports.
# (Note: Not all capabilities are necessarily showed.)
    capabilities:
      name: "Device Capabilities"
# Optional text sensor showing the active protocol version (V1/V2/auto)
    protocol:
      name: "Protocol Version"

```
All entities appear automatically in Home Assistant with native ESPHome support.

---

## 🧩 Component Architecture

| File | Purpose |
|------|---------|
| `midea_dehum.cpp` / `midea_dehum.h` | Core component class, climate control, packet dispatch |
| `midea_dehum_uart.cpp` | Low-level UART transport — frame assembly, CRC, TX/RX |
| `midea_dehum_state.cpp` | Status frame decoder — parses 36-byte status into component fields |
| `midea_dehum_features.cpp` | Optional feature implementations (ion, pump, beep, sleep, timer, capabilities) |
| `midea_dehum_protocol.h` | `ProtocolVTable` interface — one struct per version |
| `midea_dehum_protocol_v1.cpp` | Protocol v1: Chreece original handshake + status logic |
| `midea_dehum_protocol_v2.cpp` | Protocol v2: MAD50PS1QWT-A verified handshake + Midea Cube 50 support |
| `midea_dehum_protocol_auto.cpp` / `.h` | Auto-detect state machine (only compiled when `protocol_version: 0`) |
| `__init__.py` | Main component config — UART wiring, protocol selection, display modes |
| `climate.py` | Climate entity — mode, fan, humidity, swing |
| `binary_sensor.py` | Bucket full, clean filter, defrosting sensors |
| `button.py` | Filter cleaned + reset water level buttons |
| `sensor.py` | Error code, tank water level, humidity, temperature, PM2.5 sensors |
| `switch.py` | Ionizer, beep, sleep, pump switches |
| `number.py` | Timer + target humidity number entities |
| `text_sensor.py` | Device capabilities + protocol version text sensors |

---

## 🧪 Supported Features

* Power on/off
* Mode control (Setpoint, Continuous, Smart, ClothesDrying, etc.)
* Fan speed control
* Humidity control target & current humidity (via native ESPHome climate interface)
* Standalone target humidity number entity
* Standalone current humidity sensor (chartable in Home Assistant)
* Current temperature (via climate interface)
* Standalone current temperature sensor (chartable in Home Assistant)
* PM2.5 level
* Tank water level
* Bucket full status
* Defrosting status
* Clean filter request
* Filter cleaned button
* Reset water level button
* Error code reporting
* Ionizer toggle
* Vertical swing control
* Horizontal swing control
* Buzzer (beep) control on HA commands
* Pump switch
* Sleep switch
* On/Off timer
* Device capabilities discovery
* Protocol version reporting (V1/V2/auto-detect)

Note: The temperature and humidity values from the device aren't always reliable — better not use them for automations. The standalone humidity and temperature sensors are provided for monitoring/charting convenience.

---

## ⚠️ Safety Notice

Many of these dehumidifiers use R290 (Propane) as refrigerant.
This gas is flammable. Be extremely careful when opening or modifying your unit.
Avoid sparks, heat, or metal contact that could pierce the sealed system.

---

## ⚠️ Disclaimer

This project interacts directly with hardware inside a mains-powered appliance that may use R290 (propane) refrigerant.
Modifying or opening such devices can be dangerous and may cause electric shock, fire, or injury if not done safely.

By using this project, you agree that:

You perform all modifications at your own risk.

The author(s) and contributors are not responsible for any damage, data loss, or injury.

Always disconnect power before working on the device.

Never operate the unit open or modified near flammable materials.

If you’re not confident working with electrical components, don’t attempt this modification.

---

## 🧑‍💻 Credits


👉 [Hypfer/esp8266-midea-dehumidifier](https://github.com/Hypfer/esp8266-midea-dehumidifier)

Swing control and native humidity integration contributed by [CDank](https://github.com/CDank) — huge thanks for the collaboration and implementation help!

It builds upon reverse-engineering efforts and research from:

[**Mac Zhou**](https://github.com/mac-zhou/midea-msmart)

[**NeoAcheron**](https://github.com/NeoAcheron/midea-ac-py)

[**Rene Klootwijk**](https://github.com/reneklootwijk/node-mideahvac)

[**Anteater**](https://github.com/Anteater-GitHub/ESPHome_UART_Dongle) (Handshake + pump control)

[**sirwolfgang**](https://github.com/sirwolfgang) (protocol v2 + tests)
---

## 📜 License

This port follows the same open-source spirit as the original project.
See [LICENSE](https://github.com/Chreece/ESPHome-Dehumidifier/blob/main/LICENSE) for details.

<div align="center"> <sub> Made with ❤️ by <a href="https://github.com/Chreece">Chreece</a> — This project is based on <a href="https://github.com/Hypfer/esp8266-midea-dehumidifier">Hypfer's esp8266-midea-dehumidifier</a>, originally licensed under the Apache License 2.0.<br> Modifications and ESPHome integration © 2025 Chreece.<br> Original logo © Hypfer, used here for attribution under the Apache License 2.0. </sub> </div>
