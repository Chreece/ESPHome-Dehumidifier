<div align="center">
  <img src="https://github.com/Hypfer/esp8266-midea-dehumidifier/blob/master/img/logo.svg" width="800" alt="esp8266-midea-dehumidifier">
  <h2>Free your dehumidifier from the cloud — now with ESPHome</h2>
</div>

This project is an **ESPHome-based port** of [Hypfer’s esp8266-midea-dehumidifier](https://github.com/Hypfer/esp8266-midea-dehumidifier).  
While the original version used a custom MQTT firmware, this one is a **native ESPHome component**, providing full **Home Assistant integration** without MQTT or cloud dependencies.

---

## ✨ Features

This component allows you to directly control and monitor Midea-based dehumidifiers via UART, completely bypassing the Midea cloud dongle.

Supported entities:

| Entity Type     | Description |
|------------------|-------------|
| **Climate**      | Power, mode, fan speed, and humidity setpoint |
| **Binary Sensor**| "Bucket Full" indicator (always included) |
| **Sensor (optional)** | Reports current error code (optional in YAML) |
| **Switch (optional)** | Controls ionizer state if supported |

Optional entities can be included or excluded simply by adding or omitting them from your YAML.

---

### ⚠️ Entity Notes (ESPHome Limitation)

> **Note:**  
> ESPHome currently does **not support a native `humidifier` entity**, which would normally expose both current and target humidity levels.  
>  
> To work around this, this project uses a **`climate` entity** instead.  
> The target humidity is represented using the **temperature setpoint field**, and the value shown in **°C** is actually the **humidity in %**.  
>  
> So, whenever you see a temperature in the UI (e.g. “50 °C”), it means **50 % humidity** — not temperature.

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
* Inventor Eva II PRO Wi-Fi  
* Inventor EVA ION PRO Wi-Fi 20L  
* Midea Cube 20 / 35 / 50  

Models without USB or Wi-Fi button (e.g., Comfee MDDF-20DEN7) also work with small wiring changes.

---

## 🧰 Hardware Setup

You’ll need:

* **ESP32** (or ESP8266) board  
* **UART connection** (TX/RX) to your dehumidifier’s WiFi module port  
* **3.3 V ↔ 5 V level shifting** (if necessary)  
* Power supply (e.g. via USB or directly from the dongle connector)

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
    type: arduino

external_components:
  - source:
      type: git
      url: https://github.com/Chreece/ESPHome-Dehumidifier
      ref: main
    components: [midea_dehum]

uart:
  id: uart_midea
  tx_pin: GPIO16
  rx_pin: GPIO17
  baud_rate: 9600

midea_dehum:
  id: midea_dehum_comp
  uart_id: uart_midea

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

# Optional ionizer control, add this block only if your device has Ionizer
switch:
  - platform: midea_dehum
    midea_dehum_id: midea_dehum_comp
    ionizer:
      name: "Ionizer"
```

All entities appear automatically in Home Assistant with native ESPHome support.

🧩 Component Architecture
This ESPHome port introduces a modular structure, with independent sub-components:

File	Purpose
midea_dehum.cpp/h	Core UART communication and protocol handling
climate.py	Main control entity (mode, fan, humidity, etc.)
binary_sensor.py	"Bucket full" status
sensor.py	Optional error code reporting
switch.py	Optional ionizer control


🧪 Supported Features

Power on/off

Mode control (Setpoint, Continuous, etc.)

Fan speed control

Humidity setpoint

Bucket full status

Error code reporting (optional)

Ionizer toggle (if supported)

⚠️ Safety Notice
Many of these dehumidifiers use R290 (Propane) as refrigerant.
This gas is flammable. Be extremely careful when opening or modifying your unit.
Avoid sparks, heat, or metal contact that could pierce the sealed system.

🧱 Development Notes
Written in modern C++ for ESPHome 2025+

Compatible with both Arduino and ESP-IDF frameworks

Modular design: sensor and switch code only compiled when configured

Implements full Midea UART protocol (based on Hypfer’s reverse-engineered logic)

🧑‍💻 Credits
This project is a port and modernization of
👉 Hypfer/esp8266-midea-dehumidifier

It builds upon reverse-engineering efforts and research from:

Mac Zhou

NeoAcheron

Rene Klootwijk

📜 License
This port follows the same open-source spirit as the original project.
See LICENSE for details.

<div align="center"> <sub>Made with ❤️ by <a href="https://github.com/Chreece">Chreece</a> — based on original work by Hypfer.</sub> </div> ```