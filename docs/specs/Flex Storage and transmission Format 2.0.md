# Flex 2.0 Storage and Transmission Format Specification

## 1. Introduction

This document specifies the CBOR-based binary format used for sensor data storage and LoRa transmission in the Flex 2.0 firmware. It replaces the previous ASCII comma-separated LoRa format with CBOR for both storage and transmission.

### Scope

- **Storage**: Flash records (120 bytes max + 2-byte CRC)
- **LoRa Logger->Relay**: ~130 bytes max (1-byte ASCII prefix + CBOR payload + 1-byte CAPE IV)
- **LoRa Relay->Portal**: CBOR payload forwarded with relay metadata added

### Key Changes from v1

- Splitters represented as sensor type 3 with port + UID, no value
- 64-bit sensor UIDs (1-Wire ROM codes) for splitter-connected sensors
- Hierarchical port encoding via CBOR arrays (supports n >= 8 levels)
- Multi-entry chunking when data exceeds record/message size
- LoRa Logger->Relay switched from ASCII to CBOR
- Physical sensor type ("Sensor Type") included per entry for all non-temperature sensor types
- Dual-temperature sensors (type 99) with position field for multi-value probes
- Optional units field (key 22) for explicit measurement unit annotation

## 2. Map Key Registry

### Record-Level Keys

| Key | Type     | Description                                    | Context                          |
|-----|----------|------------------------------------------------|----------------------------------|
| 1   | uint32   | Timestamp (UNIX seconds)                       | All formats                      |
| 2   | array    | Sensor samples array                           | All formats                      |
| 6   | uint16   | Battery voltage (mV)                           | Storage; LoRa first chunk only   |
| 8   | uint8    | Total chunks for this sample                   | All (omit if 1)                  |
| 9   | uint8    | Chunk number (0-based)                         | All (omit if 1)                  |
| 10  | uint8    | Chunk sequence (disambiguates same-ts samples) | All (omit if 0)                  |
| 11  | uint16   | FW version packed (major*10000+minor*100+patch)| LoRa Logger->Relay first chunk   |
| 12  | uint64   | Logger device ID (MAC address)                 | LoRa Logger->Relay               |
| 13  | uint8    | Packet number (0-99)                           | LoRa Logger->Relay               |
| 14  | uint16   | Relay ICCID (last 4 digits as integer)         | LoRa Logger->Relay               |
| 15  | uint16   | Relay FW version (packed)                      | Relay->Portal                    |
| 16  | uint16   | Relay battery mV                               | Relay->Portal                    |
| 17  | int8     | Relay signal quality                           | Relay->Portal                    |
| 18  | int16    | Logger RSSI (dBm)                              | Relay->Portal                    |
| 19  | uint8    | Flags (bit 0: isParent, bit 1: isReclaimed)    | Relay->Portal                    |

### Sensor Sample Keys (inside array at key 2)

| Key | Type          | Description                                              | Required                            |
|-----|---------------|----------------------------------------------------------|-------------------------------------|
| 3   | uint / [+uint]| Port (integer for flat, array for hierarchical)          | Yes                                 |
| 4   | uint8         | Value type: 1=temp, 2=humidity (omit if 1)               | No (default=1)                      |
| 5   | float16       | Sensor value                                             | Yes, except splitters               |
| 7   | uint8         | Sensor type: 1=temp, 2=humidity, 3=splitter, 99=dual-temp| Omit only for temperature (1); required otherwise |
| 20  | uint64        | Sensor/splitter UID (64-bit 1-Wire ROM)                  | Splitters: required; sensors: optional |
| 21  | uint8         | Position within multi-value sensor (1-based)             | Only for dual-temperature (type 99) |
| 22  | uint8         | Units: 1=°C, 2=%RH (see units enum)                     | No (optional)                       |

## 3. Shared CDDL Prelude

```cddl
; -- Shared type definitions --

timestamp  = uint           ; UNIX seconds (32-bit)
battery-mv = uint .size 2   ; millivolts, max 65535
fw-version = uint .size 2   ; major*10000 + minor*100 + patch

flat-port  = uint           ; 0..9
hier-port  = [+ uint]       ; hierarchical, e.g. [2, 1, 1]
port       = flat-port / hier-port

value-type = &(
  temperature: 1,
  humidity:    2,
)

sensor-type = &(
  temperature:      1,
  humidity:         2,
  splitter:         3,
  dual-temperature: 99,
)

sensor-value = float16

units = &(
  celsius:    1,              ; °C
  percent-rh: 2,              ; %RH
)

sensor-sample = {
  3  => port,                          ; port (required)
  ? 4  => value-type,                  ; value type (omit if 1 = temperature)
  ? 5  => sensor-value,                ; value (omit for splitters)
  ? 7  => sensor-type,                 ; sensor type (omit only for temperature sensors)
  ? 20 => uint .size 8,               ; 64-bit UID (required for splitters, optional otherwise)
  ? 21 => uint .size 1,               ; position within multi-value sensor (1-based)
  ? 22 => units,                       ; measurement units (optional)
}

chunk-fields = (
  ? 8  => uint .size 1,               ; total chunks (omit if 1)
  ? 9  => uint .size 1,               ; chunk number, 0-based (omit if 1)
  ? 10 => uint .size 1,               ; chunk sequence (omit if 0)
)
```

## 4. Storage Format

### CDDL

```cddl
storage-record = {
  1 => timestamp,
  2 => [+ sensor-sample],
  ? 6 => battery-mv,                 ; present in first chunk only
  chunk-fields,
}
```

A 16-bit CRC follows the CBOR payload (not part of CBOR itself).

### CBOR Diagnostic Notation Sample

Simple case: 9 temperature probes (port 0 = ambient, ports 1-8) + battery.

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 0, 5: 23.5},
    {3: 1, 5: 12.78},
    {3: 2, 5: 14.5},
    {3: 3, 5: 15.0},
    {3: 4, 5: 16.25},
    {3: 5, 5: 17.0},
    {3: 6, 5: 18.5},
    {3: 7, 5: 19.75},
    {3: 8, 5: 20.0}
  ],
  6: 3800
}
```

### JSON Equivalent

```json
{
  "timestamp": 1745261597,
  "samples": [
    {"port": 0, "value": 23.5},
    {"port": 1, "value": 12.78},
    {"port": 2, "value": 14.5},
    {"port": 3, "value": 15.0},
    {"port": 4, "value": 16.25},
    {"port": 5, "value": 17.0},
    {"port": 6, "value": 18.5},
    {"port": 7, "value": 19.75},
    {"port": 8, "value": 20.0}
  ],
  "batteryMv": 3800
}
```

### Byte Budget

**Simple case (8 temp probes + ambient + humidity, no splitters):**

| Component | Bytes |
|-----------|-------|
| Outer map(3) + key 1 + timestamp | 1 + 1 + 5 = 7 |
| key 2 + array(10) | 1 + 1 = 2 |
| 9 temp samples: map(2) + k3 + port + k5 + f16 | 9 * 7 = 63 |
| 1 humidity: map(3) + k3 + port + k4 + type + k5 + f16 | 9 |
| key 6 + battery | 1 + 3 = 4 |
| **Total** | **85** |
| + 2 CRC | **87** |

Headroom: 33 bytes within the 120-byte limit.

**With 2 splitters + UIDs + 4 sensors (2-level ports) + ambient + humidity:**

| Component | Bytes |
|-----------|-------|
| Outer map(3) + key 1 + timestamp | 1 + 1 + 5 = 7 |
| key 2 + array(8) | 1 + 1 = 2 |
| key 6 + battery | 1 + 3 = 4 |
| 2 splitter entries: map(3) + k3 + port + k7 + type + k20 + uid64 | 15 + 15 = 30 |
| 4 sensors with [x,y] ports: map(2) + k3 + array(2) + k5 + f16 | 4 * 9 = 36 |
| Ambient temp: map(2) + k3 + port + k5 + f16 | 7 |
| Humidity: map(3) + k3 + port + k4 + type + k5 + f16 | 9 |
| **Total** | **95** |
| + 2 CRC | **97** |

Fits within 120-byte limit with 23 bytes headroom.

## 5. LoRa Logger->Relay Format

### Wire Framing

```
| ASCII 'F' (1 byte) | CBOR payload | CAPE IV (1 byte) |
```

The `'F'` prefix is outside the CBOR payload. The relay ICCID (key 14) is inside the CBOR.

### CDDL

```cddl
lora-logger-relay = {
  1  => timestamp,
  2  => [+ sensor-sample],
  ? 6  => battery-mv,                 ; first chunk only
  ? 11 => fw-version,                 ; first chunk only
  12 => uint .size 8,                 ; logger device ID (MAC address)
  13 => uint .size 1,                 ; packet number (0-99)
  14 => uint .size 2,                 ; relay ICCID last 4 digits
  chunk-fields,
}
```

### CBOR Diagnostic Notation Sample

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 0, 5: 23.5},
    {3: 1, 5: 12.78},
    {3: 2, 5: 14.5},
    {3: 3, 5: 15.0},
    {3: 4, 5: 16.25},
    {3: 5, 5: 17.0},
    {3: 6, 5: 18.5},
    {3: 7, 5: 19.75},
    {3: 8, 5: 20.0}
  ],
  6: 3800,
  11: 10200,
  12: 2659818279498972198,
  13: 1,
  14: 5678
}
```

### JSON Equivalent

```json
{
  "timestamp": 1745261597,
  "samples": [
    {"port": 0, "value": 23.5},
    {"port": 1, "value": 12.78},
    {"port": 2, "value": 14.5},
    {"port": 3, "value": 15.0},
    {"port": 4, "value": 16.25},
    {"port": 5, "value": 17.0},
    {"port": 6, "value": 18.5},
    {"port": 7, "value": 19.75},
    {"port": 8, "value": 20.0}
  ],
  "batteryMv": 3800,
  "fwVersion": 10200,
  "deviceId": 2659818279498972198,
  "packetNumber": 1,
  "relayIccid": 5678
}
```

### Byte Budget

| Component | Bytes |
|-----------|-------|
| Storage CBOR (simple 10-sensor case) | 85 |
| key 11 + fw-version | 1 + 3 = 4 |
| key 12 + device ID | 1 + 9 = 10 |
| key 13 + packet number | 1 + 1 = 2 |
| key 14 + ICCID | 1 + 3 = 4 |
| **CBOR total** | **105** |
| Wire: prefix(1) + CBOR(105) + CAPE_IV(1) | **107** |

Under the ~130-byte limit.

## 6. LoRa Relay->Portal Format

### CDDL

```cddl
lora-relay-portal = {
  1  => timestamp,
  2  => [+ sensor-sample],
  ? 6  => battery-mv,                 ; logger battery (first chunk)
  ? 11 => fw-version,                 ; logger FW version (first chunk)
  12 => uint .size 8,                 ; logger device ID (MAC address)
  13 => uint .size 1,                 ; packet number
  15 => fw-version,                   ; relay FW version
  16 => battery-mv,                   ; relay battery
  17 => int,                           ; relay signal quality (int8)
  18 => int,                           ; logger RSSI (dBm, int16)
  19 => uint .size 1,                 ; flags
  chunk-fields,
}
```

### CBOR Diagnostic Notation Sample

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 0, 5: 23.5},
    {3: 1, 5: 12.78},
    {3: 2, 5: 14.5}
  ],
  6: 3800,
  11: 10200,
  12: 2659818279498972198,
  13: 1,
  15: 10200,
  16: 4100,
  17: -85,
  18: -110,
  19: 1
}
```

### JSON Equivalent

```json
{
  "timestamp": 1745261597,
  "samples": [
    {"port": 0, "value": 23.5},
    {"port": 1, "value": 12.78},
    {"port": 2, "value": 14.5}
  ],
  "batteryMv": 3800,
  "fwVersion": 10200,
  "deviceId": 2659818279498972198,
  "packetNumber": 1,
  "relayFwVersion": 10200,
  "relayBatteryMv": 4100,
  "relaySignalQuality": -85,
  "loggerRssi": -110,
  "flags": 1
}
```

## 7. Splitter Representation

### Sensor Types

| Value | Name             | Has value type (key 4)? | Has value (key 5)? | Has UID (key 20)? | Description                          |
|-------|------------------|------------------------|--------------------|-------------------|--------------------------------------|
| 1     | Temperature      | Yes (default, omitted) | Yes (°C)           | Optional          | Temperature probe reading            |
| 2     | Humidity         | Yes                    | Yes (%RH)          | Optional          | Humidity sensor reading              |
| 3     | Splitter         | No                     | No                 | Yes (required)    | Splitter node, defines port hierarchy|
| 99    | Dual-temperature | No (temperature, omitted) | Yes (°C)        | Optional          | Dual-temperature probe (position required) |

### Rules

- Splitter entries have `sensor-type = 3` (key 7), a UID (key 20), and **no value** (key 5 absent) and **no value type** (key 4 absent).
- Sensors behind splitters use hierarchical port arrays: `[parent, child]`, `[parent, child, grandchild]`, etc.
- Invalid or disconnected sensors are omitted entirely from the samples array.

### Worked Example: Chained Splitters

Physical setup:
- Port 2 has a splitter (UID `0x5932_1234_5678_9A26`)
- Sub-port 2.1 has another splitter (UID `0x55B2_1234_5678_9A26`)
- Actual sensor at port 2.1.1
- Ambient sensor at port 0
- Humidity sensor at port 4 (produces both temperature and humidity readings)

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 2, 7: 3, 20: 6425986139893457446},
    {3: [2, 1], 7: 3, 20: 6173082339893457446},
    {3: [2, 1, 1], 5: 12.78},
    {3: 0, 5: 23.5},
    {3: 4, 5: 12.78, 7: 2, 22: 1},
    {3: 4, 4: 2, 7: 2, 5: 45.65, 22: 2}
  ],
  6: 3800
}
```

JSON equivalent:

```json
{
  "timestamp": 1745261597,
  "samples": [
    {"port": 2, "sensorType": 3, "uid": 6425986139893457446},
    {"port": [2, 1], "sensorType": 3, "uid": 6173082339893457446},
    {"port": [2, 1, 1], "value": 12.78},
    {"port": 0, "value": 23.5},
    {"port": 4, "value": 12.78, "sensorType": 2, "units": "cel"},
    {"port": 4, "valueType": 2, "sensorType": 2, "value": 45.65, "units": "%RH"}
  ],
  "batteryMv": 3800
}
```

**Entry breakdown:**

| Index | Port      | Type     | Description |
|-------|-----------|----------|-------------|
| 0     | 2         | Splitter | Top-level splitter on port 2 (sensor-type=3 via key 7, UID via key 20) |
| 1     | [2, 1]    | Splitter | Chained splitter on sub-port 2.1 (sensor-type=3 via key 7, UID via key 20) |
| 2     | [2, 1, 1] | Temp     | Sensor at port 2.1.1 (temperature — sensor type omitted) |
| 3     | 0         | Temp     | Ambient sensor (temperature — sensor type omitted) |
| 4     | 4         | Temp     | Temperature reading from humidity sensor (sensor-type=2 via key 7) |
| 5     | 4         | Humidity | Humidity reading from same sensor (value-type=2 via key 4, sensor-type=2 via key 7) |

## 8. Chunking

When a sample's CBOR exceeds 120 bytes (the storage record limit), it is split into multiple chunks. The Relay→Portal format carries whatever chunks the storage format produced, plus relay metadata.

### Keys

| Key | Purpose |
|-----|---------|
| 8   | Total chunks (omit if 1) |
| 9   | Chunk number, 0-based (omit if 1) |
| 10  | Chunk sequence (omit if 0; disambiguates samples with same timestamp) |

### Rules

- Chunks share the same timestamp (key 1).
- Receiver reassembles by matching timestamp + chunk sequence.
- **First chunk** (key 9 = 0) includes battery (key 6) and FW version (key 11 in LoRa).
- **Subsequent chunks** (key 9 > 0) omit battery and FW version.

### Worked Example: 2-Chunk Storage Record

A sample with too many sensors for one record gets split:

**Chunk 0 of 2:**

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 0, 5: 23.5},
    {3: 1, 5: 12.78},
    {3: 2, 5: 14.5},
    {3: 3, 5: 15.0},
    {3: 4, 5: 16.25},
    {3: 5, 5: 17.0}
  ],
  6: 3800,
  8: 2,
  9: 0
}
```

**Chunk 1 of 2:**

```cbor-diag
{
  1: 1745261597,
  2: [
    {3: 6, 5: 18.5},
    {3: 7, 5: 19.75},
    {3: 8, 5: 20.0},
    {3: 9, 5: 21.5}
  ],
  8: 2,
  9: 1
}
```

Note: Chunk 1 omits battery (key 6).

### Worked Example: Multi-Chunk with Splitters, Humidity, and Dual-Temperature Sensors

Physical setup:
- Port 1 has a splitter with 2 temperature probes (1.1, 1.2)
- Port 4 has a humidity sensor (produces temperature + humidity readings)
- Port 2 has a chained splitter tree: 2 → 2.1 (splitter) → 2.1.1, 2.1.2; 2 → 2.2 (splitter) → 2.2.1, 2.2.2
- Port 3.1 has a humidity sensor (temperature + humidity)
- Port 3.2 has a dual-temperature probe (positions 1 and 2)

**Chunk 1 of 2** (with battery):

```cbor-diag
{
  1: 1732617901,
  2: [
    {3: 1, 7: 3, 20: 6427063417989046822},
    {3: [1, 1], 5: 11.78},
    {3: [1, 2], 5: 12.78},
    {3: 4, 5: 12.78, 7: 2, 22: 1},
    {3: 4, 4: 2, 7: 2, 5: 45.65, 22: 2},
    {3: 2, 7: 3, 20: 6239319608523039270},
    {3: [2, 1], 7: 3, 20: 6167262014485111334},
    {3: [2, 1, 1], 5: 12.78}
  ],
  6: 3800,
  8: 2,
  9: 0
}
```

**Chunk 2 of 2** (no battery):

```cbor-diag
{
  1: 1732617901,
  2: [
    {3: [2, 1, 2], 5: 12.78, 20: 6095204420447183398},
    {3: [2, 2], 7: 3, 20: 6311377202560967206},
    {3: [2, 2, 1], 5: 12.78},
    {3: [2, 2, 2], 5: 12.78},
    {3: [3, 1], 5: 11.78, 7: 2, 22: 1},
    {3: [3, 1], 4: 2, 7: 2, 5: 45.65, 22: 2},
    {3: [3, 2], 5: 12.78, 7: 99, 21: 1},
    {3: [3, 2], 5: 12.45, 7: 99, 21: 2}
  ],
  8: 2,
  9: 1
}
```

**Entry breakdown — Chunk 1:**

| Index | Port      | Type     | Description |
|-------|-----------|----------|-------------|
| 0     | 1         | Splitter | Splitter on port 1 (sensor-type=3, UID) |
| 1     | [1, 1]    | Temp     | Temperature probe at port 1.1 |
| 2     | [1, 2]    | Temp     | Temperature probe at port 1.2 |
| 3     | 4         | Temp     | Temperature reading from humidity sensor (sensor-type=2) |
| 4     | 4         | Humidity | Humidity reading (value-type=2, sensor-type=2) |
| 5     | 2         | Splitter | Top-level splitter on port 2 (sensor-type=3, UID) |
| 6     | [2, 1]    | Splitter | Chained splitter at port 2.1 (sensor-type=3, UID) |
| 7     | [2, 1, 1] | Temp     | Temperature probe at port 2.1.1 |

**Entry breakdown — Chunk 2:**

| Index | Port      | Type      | Description |
|-------|-----------|-----------|-------------|
| 0     | [2, 1, 2] | Temp      | Temperature probe at port 2.1.2 (with UID) |
| 1     | [2, 2]    | Splitter  | Chained splitter at port 2.2 (sensor-type=3, UID) |
| 2     | [2, 2, 1] | Temp      | Temperature probe at port 2.2.1 |
| 3     | [2, 2, 2] | Temp      | Temperature probe at port 2.2.2 |
| 4     | [3, 1]    | Temp      | Temperature reading from humidity sensor (sensor-type=2) |
| 5     | [3, 1]    | Humidity  | Humidity reading (value-type=2, sensor-type=2) |
| 6     | [3, 2]    | Dual-temp | Dual-temperature probe 1 (sensor-type=99, position=1) |
| 7     | [3, 2]    | Dual-temp | Dual-temperature probe 2 (sensor-type=99, position=2) |

**Byte budget (float16):**

Chunk 0 — ~113 bytes (within 120-byte limit):

| Component | Bytes |
|-----------|-------|
| Outer map(5) + key 1 + timestamp + key 6 + battery + chunk keys (8,9) | ~18 |
| 3 splitters with UIDs: map(3) + k3 + port + k7 + type + k20 + uid64 | ~47 |
| 3 temp + 1 humidity + 1 temp-from-humidity | ~48 |
| **Total** | **~113** |

Chunk 1 — ~120 bytes (within 120-byte limit):

| Component | Bytes |
|-----------|-------|
| Outer map(3) + key 1 + timestamp + chunk keys (8,9) | ~13 |
| 1 splitter with UID | ~17 |
| 1 temp with UID: map(3) + k3 + array(3) + k5 + f16 + k20 + uid64 | ~20 |
| 4 temp/humidity entries | ~44 |
| 2 dual-temp entries: map(4) + k3 + array(2) + k5 + f16 + k7 + type + k21 + pos | ~26 |
| **Total** | **~120** |

> **Note:** `diag2cbor.rb` encodes non-f16-representable values (e.g. 12.78) as float32/64,
> so actual encoded sizes from the validator will be larger. The byte budgets above assume
> float16 encoding as used by the firmware.

### Collision Avoidance

If two different samples happen to share the same timestamp, key 10 (chunk sequence) differentiates them. The first sample uses sequence 0 (omitted), the second uses sequence 1, etc.

## 9. Port Encoding Reference

| Physical Location     | Encoding       | CBOR Diagnostic |
|-----------------------|----------------|-----------------|
| Ambient               | flat integer   | `3: 0`          |
| Port 1                | flat integer   | `3: 1`          |
| Port 5                | flat integer   | `3: 5`          |
| Port 2, sub-port 1    | array          | `3: [2, 1]`     |
| Port 2, sub-port 1, sub-port 1 | array | `3: [2, 1, 1]` |
| 8-level deep chain    | array          | `3: [2, 1, 3, 1, 2, 1, 4, 1]` |

- Flat ports (0-9): plain integer.
- Hierarchical ports (splitter chains): CBOR array of unsigned integers, one per level.
- Supports n >= 8 levels of nesting.

## 10. Design Rationale

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Port encoding | Array vs. string | Arrays are more compact in CBOR than dotted strings ("2.1.1" = 5 bytes vs. [2,1,1] = 4 bytes). Native integer parsing, no string allocation. |
| Invalid sensors | Omit entirely | Saves space vs. encoding NaN or sentinel values. Receiver infers absence. |
| LoRa framing | ASCII 'F' prefix outside CBOR | Relay identifies packet format by first byte without CBOR parsing. Backward compatible with existing relay logic. |
| Splitter as sensor type | type=3, no value | Avoids separate data structure. Splitters naturally appear in the sensor array with their UID. |
| Chunking ID | Timestamp + sequence | Avoids allocating separate sample IDs. Timestamps are already unique per measurement cycle; sequence handles collisions. |
| Units field | Optional integer enum | Units are derivable from value type (1→°C, 2→%RH), so the field is optional. Integer codes (1 byte) instead of strings ("cel"=4 bytes) for CBOR compactness. Present in JSON output for clarity. |
| Value type default | 1 (temperature) | Temperature is the most common reading. Omitting key 4 saves 2 bytes per entry for the majority of samples. |
| Sensor type field | Omit only for temperature (1); required otherwise | Temperature is the most common sensor type, so omitting saves bytes for the majority case. All non-temperature sensor types (humidity, splitter, dual-temp) must be explicit. |
| CBOR over ASCII for LoRa | CBOR | Structured data with no parsing ambiguity. More compact for binary values (UIDs, integers). |

## Appendix A. Current (v1) Format Samples

The v1 format used integer ports, string/integer types, and did not support splitters or chunking.

Use [cbor.me](https://cbor.me/) to convert to CBOR binary representation.
All examples are based on the CBOR diagnostic notation.

### Option 1 (Numeric Ports and Types)

- Ports as numbers (0 = ambient, 1-8 = external)
- Types as numbers: 1 = temperature, 2 = humidity

```cbor-diag
{
  1: 1745261597,
  2: [{
      3: 0,
      4: 1,
      5: 87.5
    },
    {
      3: 1,
      4: 1,
      5: 87.5
    },
    {
      3: 2,
      4: 1,
      5: 87.5
    },
    {
      3: 3,
      4: 1,
      5: 87.5
    },
    {
      3: 4,
      4: 1,
      5: 87.5
    },
    {
      3: 5,
      4: 1,
      5: 87.5
    },
    {
      3: 6,
      4: 1,
      5: 87.5
    },
    {
      3: 7,
      4: 1,
      5: 87.5
    },
    {
      3: 8,
      4: 1,
      5: 87.5
    }],
  6: 3800,
  7: 1
}
```

97 bytes with 16-bit floating point, including CRC.
115 bytes if expanding to 10 external sensor values.

### Option 2 (String Ports and Types)

- Type as string with 2 characters
- Port as string with 2 characters

```cbor-diag
{
  1: 1745261597,
  2: [{
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    },
    {
      3: "1a",
      4: "Ta",
      5: 87.5
    }]
}
```

113 bytes with 16-bit floats.
