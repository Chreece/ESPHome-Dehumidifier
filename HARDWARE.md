# Hardware Setup

This component connects an ESP to a compatible dehumidifier's UART interface, replacing the original Wi-Fi dongle. Communication uses **9600 baud, 8 data bits, no parity, and 1 stop bit**.

## Hardware requirements

- An ESP board with accessible UART TX/RX pins.
- A connection to the appliance's UART signals, such as a passive connector breakout or a suitable purpose-built adapter.
- A regulated supply compatible with the ESP board.
- Logic-level conversion wherever the appliance and ESP signal levels differ.

Verify the appliance interface and the ESP board's pinout before connecting them. A matching connector shape does not establish electrical or protocol compatibility.

## Suitable and unsuitable adapters

Some appliances use a USB-A connector to carry power and UART. In that arrangement, the data contacts carry serial signals rather than USB communication.

| Hardware category | Suitability for the appliance connection |
|-------------------|-----------------------------------------|
| Passive connector breakout | Suitable when its contacts are wired to the verified power, ground, TX and RX signals, with regulation and level conversion as required |
| Purpose-built appliance UART adapter | Suitable when its pinout, supply and signal levels match the appliance |
| Computer USB-to-serial programmer or a development board's programming USB port | The USB side expects a USB host; it cannot connect directly to an appliance socket carrying raw UART |
| USB hub or ordinary USB peripheral | Does not provide a direct connection between the appliance UART and ESP GPIO |
| Charge-only USB cable | Does not provide access to the data contacts needed for UART |

A USB-to-serial programmer can still be used to flash an ESP from a computer. Its USB connector is not a passive breakout. Changing GPIO assignments in software does not change the adapter's electrical connections.

## Identify and connect the signals

Identify the appliance's supply, ground, MCU TX and MCU RX using documentation or a verified pinout. Check the connector's viewing orientation. Do not identify power polarity solely from wire colours or an assumed left-to-right contact count.

| Verified appliance signal | Connect to |
|---------------------------|------------|
| GND | Common ESP and level-converter ground |
| Supply | A compatible board power input or regulator |
| MCU TX | ESP RX, through level conversion if required |
| MCU RX | ESP TX, through level conversion if required |

TX and RX are named from each device's perspective: one device's transmitter connects to the other device's receiver. Labels such as D+ and D− identify connector contacts; where those contacts carry UART, confirm their actual TX/RX roles for the appliance.

Disconnect power before changing wiring. Avoid joining two power outputs when using a separate supply during development. A programmer's TX output must not share a line already driven by the appliance MCU.

## Supply voltage and signal voltage

An ESP board accepting **5 V power** does not make its GPIO pins tolerant of **5 V signals**. ESP8266 and ESP32 GPIOs use 3.3 V logic; [Espressif specifies a GPIO voltage tolerance of 3.6 V](https://docs.espressif.com/projects/esp-faq/en/latest/hardware-related/hardware-design.html#what-is-the-voltage-tolerance-of-gpios-of-esp-chips).

Do not connect a 5 V appliance TX signal directly to ESP RX. Use suitable logic-level conversion. Low baud rates do not change the electrical limits. In the other direction, direct connection is suitable only if the appliance's RX input accepts the ESP's 3.3 V output.

| Part | Requirement |
|------|-------------|
| Development board with a documented 5 V/VIN input | Use a compatible supply on that input; follow the board's power specifications |
| Bare 3.3 V ESP module | Provide regulated 3.3 V; do not connect 5 V to its supply pin |
| Logic-level converter | Use a converter suitable for the UART signals, with the correct reference supplies, channel directions and common ground |

For a 5 V appliance UART and a 3.3 V ESP, a typical level-converted connection is:

| Path | Connection |
|------|------------|
| Receive | Appliance TX → converter 5 V side → converter 3.3 V side → ESP RX |
| Transmit | ESP TX → converter 3.3 V side → converter 5 V side → appliance RX |
| References | Converter supply/reference connections and ground according to its documentation |

Direct wiring is appropriate only when both devices' signal levels are compatible or conversion is already present.

## UART configuration

Choose TX/RX pins from the documentation for the ESP board and the [ESPHome UART component](https://esphome.io/components/uart/). Prefer a hardware UART where available. Pin assignments and hardware UART availability differ between platforms.

Configure the UART for 9600 baud, 8 data bits, no parity and 1 stop bit. If serial logging shares the appliance UART, disable its serial output with `logger.baud_rate: 0` and use network logs.

For protocol discovery, set `protocol_version: 0` and `handshake_enabled: true`. If the appliance's protocol is known, select the matching version. Discovery tries the implemented protocol variants; it does not guarantee compatibility with an untested appliance.

## Troubleshooting

Enable hexadecimal UART logging to see both transmitted and received bytes. Merge these options into the existing configuration, preserving the UART's ID, pins and baud rate rather than creating a second UART or duplicate top-level keys:

```yaml
logger:
  level: DEBUG
  baud_rate: 0

uart:
  # Keep the existing id, tx_pin, rx_pin and baud_rate here.
  debug:
    direction: BOTH
    dummy_receiver: false
    after:
      timeout: 50ms
```

Keep `dummy_receiver: false` because the component already reads incoming UART data. See [ESPHome UART debugging](https://esphome.io/components/uart/#debugging).

| Observation | Check |
|-------------|-------|
| ESP does not boot or repeatedly resets | Regulated power, boot-mode configuration and startup logs |
| No transmitted or received data | Loaded firmware, component setup, UART selection and logging configuration |
| Transmitted data but no received data | Adapter type, physical TX/RX routing, common ground, signal levels and appliance interface |
| Received data but no valid status | Baud rate, framing, signal quality and protocol compatibility |

Logged TX shows that firmware attempted transmission; it does not prove the appliance received the bytes. When using automatic discovery, capture network logs from boot for 2–3 minutes to include the discovery window.

When requesting support, include the appliance model, ESP board, ESPHome version, connection details and startup/UART logs. Remove credentials before sharing configuration or logs.
