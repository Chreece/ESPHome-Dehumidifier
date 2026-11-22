# V3 UART protocol

V3 uses 35-byte status frames and a transaction ID shared by polls and controls. With `protocol_version: 0` and `handshake_enabled: true`, the ESP recognizes it from UART traffic. Select `protocol_version: 3` to pin it. The optional protocol text sensor shows the selected version.

The captures come from a May 2024 Midea MAD50P1AWS with an SK105 adapter. Other appliances presenting the same wire format use the same handler. [Original captures and implementation](https://github.com/Chreece/ESPHome-Dehumidifier/pull/51)

## Recognition and startup

| Appliance frame | Wire signature | Handling |
| --- | --- | --- |
| Long handshake ACK | 43 bytes, starts `AA 2A A1`, bytes 8–9 are `03 07`, valid additive checksum | Continue the V3 handshake and wait for a status to confirm |
| Seed or notification | 35 bytes, starts `AA 22 A1`, bytes 8–10 are `03 05 A0` | Check CRC and checksum, echo the frame, publish state |
| Poll reply | Same length and prefix, bytes 8–10 are `03 03 C8` | Check CRC and checksum, publish state |
| Control reply | Same length and prefix, bytes 8–10 are `03 02 C8` | Check CRC and checksum, correlate the sequence, publish state |

A valid short status can identify V3 during any probe, including when the ESP missed the ACK. A 36-byte legacy status can resolve a tentative V3 selection back to V1 or V2. Fixed V1 and V2 bypass discovery.

The captured factory startup begins with a 12-byte announce, followed by six acquisition advertisements about 330 ms apart, an initial network advertisement, and two connected advertisements about 11 and 13 seconds after the ACK. The first status arrived around 21 seconds after the announce. The handler reproduces this sequence. Minimum timing requirements remain unknown.

## Polls, controls and replies

The 33-byte poll uses message type `03` and body prefix `41 81 00 FF`. The 34-byte control uses message type `02` and body prefix `48`. Both append an 8-bit transaction ID, the body CRC8 including that ID, then the additive frame checksum. A reply echoes the ID. Ordinary writes wait for the first appliance status so they preserve the current power, mode, fan, humidity and timer settings.

| Field (zero-based frame offset) | Observed values |
| --- | --- |
| Power (11) | Control `02` off or `03` on, plus bit 6 for the command beep |
| Mode (12) | `01` manual, `02` continuous, `03` smart |
| Fan (13) | Low and High read as `28` and `50`, and write as `A8` and `D0` |
| Timers (14–16) | Echo the raw timer fields during unrelated writes |
| Target humidity (17) | Direct percentage |
| Pump (19) | Explicit control `18` on or `10` off. Other commands leave the pump flag clear. The readback uses bit 3 |
| Current humidity (26) | Direct percentage |
| Reply transaction ID (32) | Echoes the poll or command sequence |

Factory transactions verify power, humidity, pump and polling on the wire. The optional timer write uses quarter-hour encoding and the filter acknowledgement uses bit 7 of byte 19, both inferred from V2. These two controls still need physical confirmation. The water-level reset has no captured V3 command.
