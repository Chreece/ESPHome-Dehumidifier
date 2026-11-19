# Hardware Setup

On supported Midea-based dehumidifiers, the original Wi-Fi dongle connects to the appliance MCU over UART. An ESP running this component replaces that dongle. The connection uses 9600 baud, 8 data bits, no parity, and 1 stop bit.

## A USB-shaped socket can carry UART

The Wi-Fi socket on supported models uses a USB-A connector for power and UART signals. It is **not a normal USB data connection**.

Use a **passive USB-A breakout with exposed contacts**, or a purpose-built adapter for the verified appliance interface, to reach the UART signals. A computer USB-to-serial programmer is a different device.

For example, the [Joy-IT SBC-ESP8266-PROG](https://joy-it.net/en/products/SBC-ESP8266-PROG) programs an ESP-01 through a computer's USB port. Plugging that programmer directly into an appliance's UART socket does not connect the appliance UART to the ESP's GPIO pins. Changing `tx_pin` and `rx_pin` in YAML cannot bypass the programmer's USB interface. Its `Prog`/`UART` switch selects programming or normal operation; it does not convert the USB plug into a passive UART connector. See the [Joy-IT manual](https://joy-it.net/files/files/Produkte/SBC-ESP8266-Prog/SBC-ESP8266-Prog-Manual_2023-12-22.pdf).

## Verify the appliance interface first

For an unlisted model, identify the original Wi-Fi module, connector orientation, supply, ground, and UART signal levels before connecting an ESP. A matching brand or connector shape does not establish electrical or protocol compatibility.

The table below describes **signal roles**, not physical contact numbering:

| Verified appliance signal | ESP connection |
|---------------------------|----------------|
| GND | Common GND |
| Supply | Board's supported power input, through a regulator if required |
| MCU TX | ESP RX, through suitable level conversion if required |
| MCU RX | ESP TX, through suitable level conversion if required |

Existing project builds report MCU TX on the contact labelled D− and MCU RX on D+. These labels identify contacts; the signals are UART, not USB. Confirm the mapping for your appliance and adapter. Do not infer power polarity from a left-to-right pin count, cable colour, or a picture viewed from the opposite side.

**Correction to earlier documentation:** the previous numbered connector table was ambiguous and must not be used to identify power or ground. Verify these signals for the actual connector before powering a build. Disconnect power before changing wiring.

## Power and logic levels are separate

A board accepting **5 V power** does not mean its GPIO pins accept **5 V signals**. ESP8266 and ESP32 GPIOs use 3.3 V logic. [Espressif specifies a GPIO voltage tolerance of 3.6 V](https://docs.espressif.com/projects/esp-faq/en/latest/hardware-related/hardware-design.html#what-is-the-voltage-tolerance-of-gpios-of-esp-chips).

Do not connect a 5 V MCU TX signal directly to an ESP RX pin. Use suitable logic-level conversion. Low baud rates and short pulses do not make 5 V GPIO input acceptable. Earlier documentation suggesting otherwise was incorrect.

The MAD50PS1QWT-A build in this project uses 5 V UART logic; other models need verification. The reverse direction also needs checking: a 3.3 V ESP TX signal is only suitable for direct connection if the appliance RX input specifications accept it.

| Hardware | Power requirement |
|----------|-------------------|
| ESP development board with a documented 5 V/VIN input | Feed that input only from a verified compatible supply; the board regulates it for the ESP |
| Bare ESP-01 module | Use a suitable regulated 3.3 V supply; do not connect 5 V to its VCC |
| Level converter | Connect its reference supplies and ground according to its documentation |

Direct UART wiring is appropriate only when both devices' logic levels are compatible, or suitable level conversion is already present. Do not leave a programmer's TX output connected to a line also driven by the appliance MCU.

## Example: XIAO ESP32C6 with level conversion

The project's MAD50PS1QWT-A example uses a Seeed Studio XIAO ESP32C6 and a BSS138 level-shifter module at 9600 baud. The connections below are by **verified signal name**; they are not a universal appliance pinout. Check the level-converter module's channel labels, pull-ups, and suitability for the UART signals.

| Connection | Appliance side | ESP / low-voltage side |
|------------|----------------|------------------------|
| Power | Verified 5 V supply to XIAO 5 V input and converter HV | XIAO 3V3 to converter LV |
| Ground | Appliance GND | XIAO GND and converter GND |
| Receive channel | MCU TX to converter high-voltage channel | Matching low-voltage channel to D7 / GPIO17 RX |
| Transmit channel | MCU RX to converter high-voltage channel | Matching low-voltage channel to D6 / GPIO16 TX |

```yaml
uart:
  id: uart_midea
  tx_pin: GPIO16  # XIAO ESP32C6 D6 -> level converter -> MCU RX
  rx_pin: GPIO17  # XIAO ESP32C6 D7 <- level converter <- MCU TX
  baud_rate: 9600
```

These GPIO numbers are specific to this example. Use your board's pinout. If you use a separate power supply during development, avoid joining two power outputs or back-feeding a computer USB port.

## ESP8266 / ESP-01 UART pins

For an ESP-01 with its UART connected to the appliance through the appropriate adapter and level conversion, the usual hardware UART assignment is TX GPIO1 and RX GPIO3. Disable serial logging on the same pins with `logger.baud_rate: 0`, and read logs over Wi-Fi.

ESPHome can use TX GPIO3 and RX GPIO1 as a **software UART**. That reversed assignment is not inherently invalid, but it does not change board traces or turn a USB programmer into a passive adapter. Prefer the normal hardware UART pins when wiring a new ESP-01 connection. See [ESPHome's UART documentation](https://esphome.io/components/uart/#hardware-uarts).

## Collecting diagnostic logs

After the hardware connection is verified, merge the following into the existing configuration. Replace the corresponding `logger`, `uart`, and `midea_dehum` entries rather than creating duplicate top-level keys. Keep the existing Wi-Fi, API, external-component, and entity configuration. This example assumes ESP-01 hardware UART wiring; other boards need their own pins.

```yaml
logger:
  level: DEBUG
  baud_rate: 0

uart:
  id: uart_midea
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 9600
  data_bits: 8
  parity: NONE
  stop_bits: 1
  debug:
    direction: BOTH
    dummy_receiver: false
    after:
      timeout: 50ms

midea_dehum:
  id: midea_dehum_comp
  uart_id: uart_midea
  protocol_version: 0
  handshake_enabled: true
  handshake_fallback_timeout: 15000
  status_poll_interval: 1000
```

The UART debugger logs hexadecimal data by default. Keep `dummy_receiver: false` because `midea_dehum` already reads incoming bytes. See [ESPHome UART debugging](https://esphome.io/components/uart/#debugging).

For an unknown protocol, use `protocol_version: 0` **together with** `handshake_enabled: true` to start discovery. Capture network logs from boot for 2–3 minutes so the 120-second discovery window is included. Auto-detection tries the implemented protocol variants; it does not establish compatibility with every appliance.

With a pinned protocol and `handshake_enabled: false`, the current component starts regular polling without waiting for the handshake fallback timer. A missing response in that configuration still needs UART evidence before attributing it to startup logic.

| Observation | Next check |
|-------------|------------|
| ESP boots, but the whole USB programmer is plugged into the appliance | Replace that data path with a verified direct UART connection |
| TX appears in logs, but no RX | Check adapter type, actual pin routing, common ground, levels, appliance power, and interface compatibility; logged TX does not prove the MCU received it |
| Neither TX nor RX appears | Include the startup log and verify that the intended firmware, UART and component are running |
| RX bytes appear, but no valid status | Share the raw bytes; check baud/framing and then protocol or model support |
| ESP does not boot or repeatedly resets | Check regulated supply, boot-mode configuration, and startup logs |

For a support report, include the exact appliance model, ESP board/module, ESPHome version, photos of the adapter and connection, the original Wi-Fi module label if available, and the boot/UART logs. Remove Wi-Fi passwords, API keys, and other credentials before posting configuration or logs.
