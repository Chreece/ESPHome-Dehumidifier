# Midea MAD50PS1QWT-A Dehumidifier Protocol

Reverse-engineered UART protocol for the Midea MAD50PS1QWT-A (50-pint) dehumidifier.
All frames verified against logic analyzer captures of the original RTL8720 WiFi dongle.

> **Relationship to ESPHome code:** This document describes the raw wire protocol
> between the MCU and the RTL8720 dongle. The ESPHome implementation in
> `midea_dehum_protocol_v2.cpp` replicates the **handshake and status polling** frames
> exactly. However, **Phase 5 control commands** use the project's existing
> `sendSetStatus()` format rather than the RTL8720 command layout — see the note
> in that section.

**Hardware:** MCU uses 5V UART @ 9600 8N1 via USB-A port
**MCU boot time:** ~4 seconds after power-on (verified 2026-06-30 via test bench)
**⚠ Wiring:** ESP/dongle TX → **D+ (Pin 3)**, ESP/dongle RX ← **D- (Pin 2)**
**Init:** Single pair (1a+1b, 43 bytes) at ~800ms intervals until MCU responds

---

## Frame Structure

```
AA LL TT [payload...] CK
│  │  │              └─ Checksum (last byte)
│  │  └─ Device type (A1 = dehumidifier, FF = network/dongle)
│  └─ Length (bytes following AA, i.e. frame length - 1)
└─ Start byte (always 0xAA)
```

---

## Verified Call/Response Table

Tested 2026-06-30 via cold-boot power cycle + USB-TTL. MCU responds ~4s after
power-on. A **single init pair** (1a+1b, 43 bytes) is sufficient.

```
  # │ Direction    │ Send                              │ Response
 ───┼──────────────┼───────────────────────────────────┼────────────────────────────
  1 │ ESP → MCU    │ 1a+1b: Announce + NetInit (43B)   │ (cycles every ~800ms)
  2 │ MCU → ESP    │                                    │ 2a. Device ACK (0x07, 43B)
  3 │ ESP → MCU    │ 3a. DongleInfoResp (12B)           │
    │              │ 3b. NetStatus Acquiring (31B)      │
  4 │ MCU → ESP    │                                    │ 2b. Info Query (0xE1, 27B)
  5 │ ESP → MCU    │ 3b. NetStatus WiFi, no IP (31B)    │
  6 │ MCU → ESP    │                                    │ 2c. Response (0xA0, 43B)
  7 │ ESP → MCU    │ 3b. NetStatus WiFi+IP (31B) ×2     │
  8 │ MCU → ESP    │                                    │ ✦ STATUS (0x05 0xA0, 36B)
  9 │ ESP → MCU    │ Status Query (15B, every 2s)       │
 10 │ MCU → ESP    │                                    │ STATUS (36B) or Caps (34B)
```

**Key findings:**
- MCU ACKs to a single init pair
- **Push-on-change**: MCU sends status unprompted when physical buttons are pressed.
  **Polling**: ESP polls to verify command execution and detect liveness.
  Both are needed — they solve different problems.
- Announce-only: gets ACK but no follow-up. NetworkInit-only: gets no response
- MCU boot is ~4s (the ~32s in captures was the RTL8720 dongle's WiFi boot delay)

---

## Phase 1: Boot Broadcast (ESP → MCU)

After power-on (~4s until MCU ready), ESP sends the init pair at ~800ms intervals
until the MCU responds with a Device ACK (2a). The pair is sent back-to-back
with zero gap between 1a and 1b (~45ms total at 9600 baud).

### 1a. Dongle Announce (12 bytes)

```
AA 0B FF F4 00 00 01 00 08 07 00 F2
```

| Offset | Value | Field |
|--------|-------|-------|
| 0 | `AA` | Start byte |
| 1 | `0B` | Length (11) |
| 2 | `FF` | Device type (network) |
| 3 | `F4` | Frame type (dongle announce) |
| 4-5 | `00 00` | Sync |
| 6-7 | `01 00` | Payload |
| 8 | `08` | Protocol version |
| 9 | `07` | Message type |
| 10 | `00` | Reserved |
| 11 | `F2` | Checksum |

### 1b. Network Init (31 bytes)

```
AA 1E FF E1 00 00 01 00 08 65 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 94
```

| Offset | Value | Field |
|--------|-------|-------|
| 0 | `AA` | Start byte |
| 1 | `1E` | Length (30) |
| 2 | `FF` | Device type (network) |
| 3 | `E1` | Frame type (network init) |
| 4-5 | `00 00` | Sync |
| 6-7 | `01 00` | Payload |
| 8 | `08` | Protocol version |
| 9 | `65` | Message type |
| 10-29 | `00...` | Padding (zeros) |
| 30 | `94` | Checksum |

---

## Phase 2: MCU Device ACK (→ ESP)

MCU responds after receiving the init pair. This is the first MCU→ESP frame.

### 2a. Device ACK (43 bytes)

```
AA 2A A1 00 00 00 00 00 08 07 FF FF FF FF FF FF FF FF
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
FF FF FF FF FF FF FF 46
```

| Offset | Value | Field |
|--------|-------|-------|
| 0 | `AA` | Start byte |
| 1 | `2A` | Length (42) |
| 2 | `A1` | Device type (dehumidifier) |
| 8 | `08` | Protocol version |
| 9 | `07` | Response type (ACK dongle) |
| 10-38 | `FF...` | Device info payload |
| 41 | `46` | Checksum |

**Trigger:** `data[9] == 0x07` — MCU acknowledges dongle.
Code reads `data[2]` → `appliance_type_`, `data[7]` → `protocol_version_`.

---

## Phase 3: Handshake Exchange

After ACK, ESP sends DongleInfoResponse + NetStatus Acquiring. MCU queries back
for identity and network info. Once MCU receives WiFi+IP status ×2, it sends a
**seed status** to populate initial state for the app. After that, status updates
are push-based — the MCU sends new status on change, not on every poll.

### MCU Queries (→ ESP)

#### 2b. Dongle Info Query (27 bytes)

```
AA 1A A1 00 00 00 00 00 08 E1 81 81 00 00 00 00 00 00
00 00 00 00 00 00 00 00 5A
```

| Offset | Value | Field |
|--------|-------|-------|
| 9 | `E1` | Query type (request dongle identity) |
| 10 | `81` | Sub-type |
| 11 | `81` | Sub-type |

MCU requests dongle identity. Respond with NetStatus WiFi (3b variant B, no IP).

#### 2c. Response (0xA0, 43 bytes)

```
AA 2A A1 00 00 00 00 00 08 A0 00 A1 03 00 16 00 00...
```

MCU responds after receiving WiFi-connected network status.
**Respond with 2× NetStatus WiFi+IP** — MCU sends seed status after the second one.

#### 2d. Network Status Request (31 bytes)

```
AA 1E A1 00 00 00 00 00 08 63 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 D6
```

| Offset | Value | Field |
|--------|-------|-------|
| 10 | `63` | Query type (request network status) |

MCU periodically asks for WiFi info. Respond with NetStatus WiFi+IP (3b variant C).

### ESP Responses (→ MCU)

#### 3a. Dongle Info Response (12 bytes)

```
AA 0B A1 AA 00 00 00 00 08 E1 00 C1
```

Sent in response to MCU Info Query (2b). Note: sending this alone triggers the E1 query —
the MCU won't accept Network Status until it first receives DongleInfoResponse.

#### 3b. Network Status (31 bytes)

**Variant A — Acquiring (no IP yet):**
```
AA 1E A1 BF 00 00 00 00 08 A0 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 DA
```

**Variant B — WiFi connected, no IP:**
```
AA 1E A1 BF 00 00 00 00 08 0D 01 01 00 00 00 00 FF 00
00 00 00 00 01 00 09 00 02 00 00 00 7C
```

**Variant C — WiFi + IP (192.168.10.110):**
```
AA 1E A1 BF 00 00 00 00 08 0D 01 01 04 6E 0A A8 C0 FF
00 00 00 00 01 00 09 00 03 00 00 00 7B
```

| Offset | Value (C) | Field |
|--------|-----------|-------|
| 3 | `BF` | Frame type |
| 9 | `0D` | Message type (0xA0 = acquiring, 0x0D = connected) |
| 10-11 | `01 01` | WiFi connected flag |
| 12-15 | `04 6E 0A A8 C0` | IP: 192.168.10.110 (bytes reversed) |
| 16-17 | `FF 00` | Subnet mask |
| 20-21 | `01 00` | DNS |
| 22 | `09` | Unknown |
| 24-25 | `03 00` | Unknown (increments each send) |
| 30 | `7B` | Checksum |

Sent unsolicited during handshake and in response to MCU `0x63` queries (2d).
Counters at bytes 24-25 increment with each send. Use any variant as appropriate
(acquiring → WiFi no-IP → WiFi+IP as network comes up).

---

## Phase 4: Steady-State Communication

After handshake, the MCU sends a **seed status** to populate initial state. From
this point, two complementary mechanisms keep the ESP in sync:

**Push-on-change** — The MCU sends status unprompted when a physical button
is pressed (power, mode, fan, humidity ±, timer). No polling needed for local
changes. Verified 2026-07-01: humidity ± presses produced 8 push events in 14s.

**Polling** — The ESP sends status queries for command verification (did the
ESP's Power OFF command take effect?) and as a periodic liveness check. The
MCU responds to each poll with current state, and may return Capabilities (0xB5)
on the first poll after handshake.

These serve different needs: push catches physical interactions instantly,
polling confirms ESP-initiated commands and detects MCU disconnection.

### Status Query (→ MCU, 15 bytes)

```
AA 0E A1 00 00 00 00 00 03 03 B5 01 11 8E F6
```

| Offset | Value | Field |
|--------|-------|-------|
| 1 | `0E` | Length (14) |
| 2 | `A1` | Device type |
| 8 | `03` | Protocol subtype |
| 9 | `03` | Query type |
| 10 | `B5` | Sub-type (status request) |
| 14 | `F6` | Checksum |

### Capabilities Response (MCU → ESP, 34 bytes)

Returned on first status query after handshake, or when MCU hasn't cached
device capabilities.

```
AA 21 A1 00 00 00 00 00 08 03 B5 05 10 02 01 03 17
02 01 02 1D 02 01 01 20 02 01 01 2D 02 01 04 C4 0A
```

| Offset | Value | Field |
|--------|-------|-------|
| 1 | `21` | Length (33) |
| 2 | `A1` | Device type |
| 9 | `03` | Protocol subtype |
| 10 | `B5` | Capabilities response type |
| 11-32 | `...` | Capability descriptors (5 groups of 4 bytes) |
| 33 | `0A` | Checksum |

After capabilities are cached, subsequent queries return Status directly.

### Status Response (MCU → ESP)

**Length:** 0x23 = 36 bytes

```
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35
    AA 23 A1 00 00 00 00 00 08 05 A0 PW MD FN 7F 7F 00 TH 00 FL TK 00 00 00 00 ?? CH TR 00 00 00 ER 00 XX XX CK
```

| Byte | Field | Description |
|------|-------|-------------|
| 11 | Power | bit 0: 0=OFF, 1=ON |
| 12 | Mode | 0x01=Set, 0x02=Continuous, 0x04=Max |
| 13 | Fan | bits 0-6: 0x28(40)=Low, 0x50(80)=High |
| 17 | Target Humidity | Setpoint % (direct value) |
| 19 | Feature Flags | See table below |
| 20 | Tank/Defrost | bits 0-6: tank %, bit 7: defrost |
| 26 | Current Humidity | Sensor reading % |
| 27 | Temperature Raw | °C = `(byte - 50) / 2` |
| 31 | Error Code | 0=OK, 0x25(37)=Eb, 0x26(38)=P2 |

#### Feature Flags (Byte 19)

On this model, byte 19 is effectively just a pump indicator. Sleep mode and
light brightness features were confirmed absent (bits 5-7 always 0 across all captures).

| Bit | Mask | Feature | Present? |
|-----|------|---------|----------|
| 7 | 0x80 | Filter cleaning request | Unverified — never triggered across all captures |
| 6-5 | 0x60 | Panel light brightness | No — always 0 |
| 4 | 0x10 | Always 1 (feature-flag indicator) | Yes |
| 3 | 0x08 | Pump/drain active | Yes — 0x10=off, 0x18=on |
| 0-2 | 0x07 | Unused | Always 0 |

> **ESPHome note:** `sendSetStatus()` writes pump state to byte 19 using the same
> 0x10/0x18 encoding, but `parseState()` does **not** read pump state back from
> byte 19 — pump state is write-only in the current implementation.

---

## Phase 5: Control Commands (ESP → MCU)

> **ESPHome note:** The project implements this via the `send_set_status`
> vtable callback in `midea_dehum_protocol_v2.cpp`. It uses the V2-verified payload
> layout shown below (power 0x42/0x43, fan 0xA8/0xD0, pump at byte 9, etc.).
> V1 devices use the default `sendSetStatus()` logic instead.

### Command Frame (34 bytes)

```
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33
    AA 21 A1 00 00 00 00 00 00 02 48 PP MM FF 7F 7F 00 HH 00 PM 00 00 00 00 00 WL 01 00 00 00 00 00 00 CK
```

### Power (Byte 11)

| Value | State |
|-------|-------|
| 0x42 | OFF |
| 0x43 | ON |

### Mode (Byte 12)

| Value | Mode |
|-------|------|
| 0x01 | Set (humidity setpoint) |
| 0x02 | Continuous |
| 0x04 | Max |

### Fan Speed (Byte 13)

| Value | Speed |
|-------|-------|
| 0xA8 | Low |
| 0xD0 | High |

### Target Humidity (Byte 17)

Direct hex value: 0x23=35% through 0x55=85%

### Pump (Byte 19)

| Value | State |
|-------|-------|
| 0x10 | OFF |
| 0x18 | ON |

### Water Level Threshold (Byte 25)

Sets bucket fullness sensitivity. Matches on-unit button behavior.
The levels are *thresholds*, not percentages — the app maps them differently:

| Value | Level | App shows |
|-------|-------|-----------|
| 0x19 | 1 | 50% |
| 0x32 | 2 | ~67% |
| 0x4B | 3 | ~83% |
| 0x64 | 4 | 100% |

---

## Timer Encoding (Command Bytes 14-16)

Physical button sets timer. Range: 0.5h-24h.

| Byte | Field | Encoding |
|------|-------|----------|
| 14 | ON timer | bit 7: set, bits 6-2: hours, bits 1-0: quarter (0=:00, 2=:30) |
| 15 | OFF timer | bit 7: set, bits 6-2: hours, bits 1-0: quarter |
| 16 | Fine offset | bits 7-4: ON fine, bits 3-0: OFF fine (usually 0x0F) |

| Timer | B15 hex | Decoded |
|-------|---------|---------|
| 0.5h | 0x82 | 0h 30m |
| 1h | 0x84 | 1h 0m |
| 1.5h | 0x86 | 1h 30m |
| 2h | 0x88 | 2h 0m |
| 5h | 0x94 | 5h 0m |
| 10h | 0xA8 | 10h 0m |
| 11h | 0xAC | 11h 0m |
| 12h | 0xB0 | 12h 0m |
| 18h | 0xC8 | 18h 0m |
| 24h | 0xE0 | 24h 0m |
| clear | 0x7F | No timer |

Note: B14 = 0x7F (no ON timer), B16 = 0x0F for all captures.

### Reset Fill Level (App-only, 36 bytes)

> **ESPHome note:** This command is not implemented in the current code.

```
AA 23 A1 00 00 00 00 00 08 03 C8 [state payload] [counter] CK
```

Differs from normal command: length 0x23, byte 9=0x03, byte 10=0xC8.

---

## Checksum

```python
def checksum(frame):
    """frame includes AA start byte; skip it, sum bytes 1..N-1"""
    return (256 - sum(frame[1:-1])) & 0xFF
```

Last byte of every frame = `(256 - sum(all_bytes_after_AA_except_last)) & 0xFF`

---

## Error Codes (Status Byte 31)

| Code | Hex | Display | Meaning |
|------|-----|---------|---------|
| 0 | 0x00 | — | No error |
| 37 | 0x25 | Eb | Bucket removed/mispositioned |
| 38 | 0x26 | P2 | Fill timer expired (per Chreece) |

Other display codes from manual (unverified byte mapping): AS (humidity sensor),
ES (tube temp sensor), EC (refrigerant leak), E3 (unit malfunction).

---

## MAD50PS1QWT-A Feature Verification

| Feature | Byte | Verified |
|---------|------|----------|
| Power ON/OFF | 11 | ✓ |
| Mode Set/Cont/Max | 12 | ✓ |
| Fan High/Low | 13 | ✓ |
| Target humidity 35-85% | 17 | ✓ |
| Current humidity | 26 | ✓ |
| Temperature | 27 | ✓ |
| Tank level | 20 (bits 0-6) | ✓ Confirmed: runtime-based counter, resets to 0 on power cycle. fan-high capture showed 0x4B=75% |
| Defrost | 20 bit 7 | Untestable (needs cold temps) |
| Sleep mode | 19 bit 4 | ✗ Not on MAD50PS1QWT-A |
| Light brightness | 19 bits 6-5 | ✗ Not on MAD50PS1QWT-A |
| Filter request | 19 bit 7 | Unverified — never triggered, but supported feature |
| Pump ON/OFF | 19 | ✓ |
| Timer 0.5h-24h | 14-16 | ✓ |
| Error OK | 31 | ✓ |
| Error Eb (37) | 31 | ✓ |
| Error P2 (38) | 31 | Unverified |
| Reset fill level | — | ✓ |

---

## Hardware Notes

- **Voltage:** 5V logic (MCU) ↔ 3.3V (ESP32) — level shifter required
- **Baud:** 9600 8N1
- **USB-A pinout (verified 2026-06-30 via USB-TTL test):**
  - **Pin 2 (D-)** = MCU TX (MCU transmits here → connect to ESP/dongle RX)
  - **Pin 3 (D+)** = MCU RX (MCU receives here → connect to ESP/dongle TX)
  - Pin 1 = GND, Pin 4 = 5V
- **Init pair:** Single pair (1a+1b, 43 bytes) at ~800ms intervals until MCU ACKs
- **Tank level:** Byte 20, bits 0-6. Runtime-based estimate (not a physical sensor).
  Resets to 0 on power cycle. Reaches 75-100% after several hours of compressor runtime.
- **MCU silent until dongle sends correct init burst with correct wiring**
