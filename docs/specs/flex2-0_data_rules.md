# Flex 2.0 Data Rules Specification

## 1. Overview

This document specifies the rules for interpreting sensor data fields in the Flex 2.0 JSON format. It covers field presence, defaults, and the resolution logic that Portal/Lambda must apply to determine sensor and value types.

These rules apply equally to both delivery paths — the JSON payload content is identical; only the outer envelope differs.

## 2. Delivery Paths

### 2.1 LTE (via Coiote shadow update)

Data arrives as an AWS IoT shadow update with `state.reported` wrapping:

```json
{
  "state": {
    "reported": {
      "EXACT Sensor": { ... },
      "EXACT Chunks": { ... }
    }
  }
}
```

### 2.2 LoRa (via Lambda post-processing)

Data arrives as a flat JSON object with `thingName` for device identification:

```json
{
  "thingName": "urn:dev:mac:129B6A594D679D87",
  "EXACT Sensor": { ... },
  "EXACT Chunks": { ... }
}
```

## 3. Top-Level Keys

| Key | Type | Description | Required |
|-----|------|-------------|----------|
| `thingName` | string | Device URN (LoRa path only) | LoRa only |
| `EXACT Sensor` | object | Numbered map of sensor entries | Yes |
| `EXACT Chunks` | object | Chunk metadata for message reassembly | Yes (every message) |
| `Device` | object | Logger metadata | Optional (first chunk) |
| `Connectivity Monitoring` | object | Signal metadata | Optional (first chunk) |

### 3.1 Device Metadata

Present in the first chunk only. Maps to shadow keys:

```json
{
  "Device": {
    "0": {
      "Power Source Voltage": "3800",
      "Firmware Version": "2.0.1"
    }
  }
}
```

### 3.2 Connectivity Monitoring

Present in the first chunk only:

```json
{
  "Connectivity Monitoring": {
    "0": {
      "Radio Signal Strength": "-85",
      "Link Quality": "7"
    }
  }
}
```

## 4. EXACT Chunks

Every message must include `EXACT Chunks`, even for single-chunk messages.

| Field | Type | Description |
|-------|------|-------------|
| `Sample ID` | string | 64-bit unsigned integer as string; groups chunks belonging to the same sample |
| `Total Chunks` | string | Total number of chunks for this sample |
| `Chunk Number` | string | 1-based chunk index |

```json
{
  "EXACT Chunks": {
    "0": {
      "Sample ID": "1770393960",
      "Total Chunks": "2",
      "Chunk Number": "1"
    }
  }
}
```

The receiver reassembles a complete sample by collecting all chunks with the same `Sample ID` until `Chunk Number` equals `Total Chunks`.

## 5. EXACT Sensor Entry Fields

Each entry in the `EXACT Sensor` map is a numbered object representing either a splitter node or a sensor value reading.

| Field | Type | Description | Presence rules |
|-------|------|-------------|----------------|
| `Port` | string | Dot-separated port path (e.g. `"1"`, `"2.1.3"`) | Always required |
| `Timestamp` | string | ISO 8601 UTC timestamp | Required for value entries |
| `Sensor Type` | string | Physical sensor type (see section 6) | See section 7 |
| `Type` | string | Value type (see section 6) | See section 7 |
| `Value` | string | Numeric reading as string | Required for value entries; absent for splitters |
| `Units` | string | Measurement unit (`"cel"`, `"%RH"`) | See section 7 |
| `UID` | string | 64-bit sensor/splitter identifier | Required for splitters; optional for sensors |
| `Position` | string | 1-based position within multi-value same-type sensor | Only for multi-value same-type sensors (e.g. dual-temp) |

All values are JSON strings for consistency with the Coiote/LTE shadow format.

## 6. Type Definitions

### 6.1 Sensor Type (physical sensor)

| Value | Name | Description | Value count |
|-------|------|-------------|-------------|
| `"1"` | Temperature | Standard analog temperature probe | Single |
| `"2"` | Humidity | Temperature + humidity sensor | Multi (temp + RH) |
| `"3"` | Splitter | Port splitter node, no readings | None |
| _TBD_ | Rope | Daisy-chained temperature sensor rope (see section 7.2) | Multi (temp × N) |
| `"5"` | Particulate Matter | Sensirion SEN5x (see section 7.2) | Multi (8: PM1.0/2.5/4.0/10, RH, temp, VOC/NOx index) |
| `"6"` | Air Quality | Bosch BME680 (see section 7.2) | Multi (6: temp, pressure, humidity, IAQ, CO2, b-VOC) |
| `"99"` | Dual-temperature | Probe with 2 temperature readings | Multi (temp + temp) |

### 6.2 Value Type (individual reading)

| Value | Name | Default Unit |
|-------|------|--------------|
| `"1"` | Temperature | `"cel"` |
| `"2"` | Humidity | `"%RH"` |
| `"3"` | PM1.0 | `"ug/m3"` |
| `"4"` | PM2.5 | `"ug/m3"` |
| `"5"` | PM4.0 | `"ug/m3"` |
| `"6"` | PM10 | `"ug/m3"` |
| `"7"` | VOC Index | `"idx"` |
| `"8"` | NOx Index | `"idx"` |
| `"9"` | Pressure | `"hPa"` |
| `"10"` | IAQ | `"idx"` |
| `"11"` | CO2 | `"ppm"` |
| `"12"` | b-VOC | `"ppm"` |

> **Pressure** is reported in hectopascals (`"hPa"`), not pascals, because the
> binary wire value is a float16 (max 65504) and atmospheric pressure in Pa
> (~101325) does not fit. Multiply by 100 to obtain Pa.

## 7. Field Presence Rules

### 7.1 Priority and resolution order

The receiver resolves each sensor entry by inspecting fields in this order:

1. **Sensor Type** — identifies the physical sensor
2. **Type** — identifies the value type (only needed for multi-value sensors with different types)
3. **Position** — disambiguates multi-value sensors with the same type
4. **Units** — present only if the unit is non-default for the value type

### 7.2 Rules by sensor type

#### Temperature sensor (Sensor Type 1) — the default

The most common case. To minimize payload size, all identifying fields may be omitted.

| Field | Rule |
|-------|------|
| `Sensor Type` | Omit (default is temperature) |
| `Type` | Omit (default is `"1"`) |
| `Units` | Omit (default is `"cel"`) |
| `Position` | Absent |

**Receiver rule**: If `Sensor Type`, `Type`, `Units`, and `Position` are all absent, the entry is a single-value temperature reading.

#### Humidity sensor (Sensor Type 2)

Produces two value entries on the same port: one temperature, one humidity.

| Entry | Sensor Type | Type | Units | Position |
|-------|-------------|------|-------|----------|
| Temperature value | `"2"` | Omit (inferable) | Omit (default) | Absent |
| Humidity value | `"2"` | `"2"` | Omit (default `"%RH"`) | Absent |

**Receiver rule**: `Sensor Type = "2"` identifies the physical sensor. The two values are distinguished by `Type`: absent/`"1"` = temperature, `"2"` = humidity.

#### Splitter (Sensor Type 3)

No readings. Defines a node in the port hierarchy.

| Field | Rule |
|-------|------|
| `Sensor Type` | `"3"` (required) |
| `Type` | Absent |
| `Value` | Absent |
| `Units` | Absent |
| `UID` | Required |

#### Rope sensor (Sensor Type TBD)

A daisy-chained sensor rope producing multiple temperature readings. Follows the same structural pattern as splitters: a parent entry at port X with child value entries at X.1, X.2, X.3, etc.

No specific Sensor Type number is reserved for rope sensors yet. Different rope types (different unit, label, etc.) will each receive their own Sensor Type when the hardware is chosen. The exact Sensor Type values will be defined at that time, requiring software work on both firmware and Portal.

**Parent entry (rope node):**

| Field | Rule |
|-------|------|
| `Sensor Type` | _TBD_ (assigned per rope type) |
| `Type` | Absent |
| `Value` | Absent |
| `Units` | Absent |
| `UID` | Required (identifies the rope) |

**Child entries (individual readings along the rope):**

| Field | Rule |
|-------|------|
| `Port` | Sub-port of parent (e.g. `"3.8.1"`, `"3.8.2"`, ...) |
| `Sensor Type` | Omit (inferable from parent) |
| `Type` | Omit (default temperature) |
| `Units` | Omit (default `"cel"`) |
| `Position` | Absent — port hierarchy provides position |
| `Value` | Required |
| `Timestamp` | Required |

**Receiver rule**: A parent entry with a rope Sensor Type groups all child sub-port entries as readings from a single rope. The sub-port index (1, 2, 3, ...) determines the physical position along the rope. Unlike the dual-temperature sensor (which uses the `Position` field), rope sensors rely on port hierarchy to order values — this is consistent with how splitters work.

#### Particulate Matter sensor (Sensor Type 5)

Sensirion SEN5x. Produces eight value entries on the same port, distinguished by
`Type`: PM1.0 (`"3"`), PM2.5 (`"4"`), PM4.0 (`"5"`), PM10 (`"6"`), humidity
(`"2"`), temperature (omit/`"1"`), VOC Index (`"7"`), NOx Index (`"8"`).

| Entry | Sensor Type | Type | Units |
|-------|-------------|------|-------|
| Temperature | `"5"` | Omit (inferable) | Omit (default `"cel"`) |
| Humidity | `"5"` | `"2"` | Omit (default `"%RH"`) |
| PM1.0 / PM2.5 / PM4.0 / PM10 | `"5"` | `"3"`/`"4"`/`"5"`/`"6"` | Omit (default `"ug/m3"`) |
| VOC Index / NOx Index | `"5"` | `"7"`/`"8"` | Omit (default `"idx"`) |

**Receiver rule**: `Sensor Type = "5"` identifies the physical sensor. The eight
values are distinguished by `Type`. `Units` may be omitted; the default unit
follows from `Type` (section 6.2).

#### Air Quality sensor (Sensor Type 6)

Bosch BME680. Produces six value entries on the same port, distinguished by
`Type`: temperature (omit/`"1"`), pressure (`"9"`), humidity (`"2"`), IAQ
(`"10"`), CO2 (`"11"`), b-VOC (`"12"`).

| Entry | Sensor Type | Type | Units |
|-------|-------------|------|-------|
| Temperature | `"6"` | Omit (inferable) | Omit (default `"cel"`) |
| Pressure | `"6"` | `"9"` | Omit (default `"hPa"`) |
| Humidity | `"6"` | `"2"` | Omit (default `"%RH"`) |
| IAQ | `"6"` | `"10"` | Omit (default `"idx"`) |
| CO2 | `"6"` | `"11"` | Omit (default `"ppm"`) |
| b-VOC | `"6"` | `"12"` | Omit (default `"ppm"`) |

**Receiver rule**: `Sensor Type = "6"` identifies the physical sensor; `Type`
distinguishes the six values. Pressure is in hPa — multiply by 100 for Pa.

#### Dual-temperature sensor (Sensor Type 99)

Multiple temperature readings from a single physical sensor, distinguished by position.

| Field | Rule |
|-------|------|
| `Sensor Type` | `"99"` (required) |
| `Type` | Omit (all values are temperature) |
| `Units` | Omit (default `"cel"`) |
| `Position` | Required (`"1"`, `"2"`, ...) |

### 7.3 Summary table

| Sensor Type | Sensor Type field | Type field | Units field | Position field |
|-------------|-------------------|------------|-------------|----------------|
| Temperature (1) | Omit | Omit | Omit | Absent |
| Humidity (2) — temp value | `"2"` | Omit | Omit | Absent |
| Humidity (2) — RH value | `"2"` | `"2"` | Omit | Absent |
| Splitter (3) | `"3"` | Absent | Absent | Absent |
| Rope (_TBD_) — parent | _TBD_ | Absent | Absent | Absent |
| Rope (_TBD_) — each child value | Omit | Omit | Omit | Absent (port hierarchy provides position) |
| Particulate Matter (5) — temp value | `"5"` | Omit | Omit | Absent |
| Particulate Matter (5) — other values | `"5"` | `"2"`–`"8"` | Omit | Absent |
| Air Quality (6) — temp value | `"6"` | Omit | Omit | Absent |
| Air Quality (6) — other values | `"6"` | `"2"`/`"9"`–`"12"` | Omit | Absent |
| Dual-temp (99) — each value | `"99"` | Omit | Omit | Required |

### 7.4 Fallback rule (exception)

If `Sensor Type` is absent but `Units` is present:
- `"cel"` (case-insensitive) → infer Sensor Type = 1 (temperature)
- `"%RH"` (case-insensitive) → infer Sensor Type = 2 (humidity)

If `Sensor Type` is absent and `Units` is absent → default to temperature.

If `Sensor Type` is absent and `Units` is unrecognized → discard the entry as invalid.

### 7.5 General rules for future sensor types

Adding a new sensor type always requires software work on both firmware and Portal. The convention is:

- **Single-value sensor**: `Sensor Type` is sufficient. `Type` and `Units` are inferable.
- **Multi-value, different types**: `Sensor Type` + `Type` (or `Units`) to distinguish values.
- **Multi-value, same type**: `Sensor Type` + `Position` to distinguish values. `Position` has higher priority than `Units` for disambiguation.
- **Multi-value via port hierarchy (rope-style)**: `Sensor Type` on a parent entry, with child value entries at sub-ports. No `Position` field needed — the port sub-path provides ordering.

## 8. JSON Samples

### 8.1 LoRa path (Lambda output)

```json
{
    "thingName": "urn:dev:mac:129B6A594D679D87",
    "EXACT Sensor": {
        "0": {
            "Port": "3",
            "Sensor Type": "3",
            "UID": "Splitter50"
        },
        "1": {
            "Port": "3.6",
            "Sensor Type": "3",
            "UID": "Splitter60"
        },
        "2": {
            "Port": "3.7",
            "Sensor Type": "3",
            "UID": "Splitter70"
        },
        "3": {
            "Port": "1",
            "Value": "1",
            "Timestamp": "2026-02-24T10:45:01Z",
            "UID": "1111"
        },
        "4": {
            "Port": "2",
            "Value": "1",
            "Timestamp": "2026-02-24T10:45:01Z",
            "UID": "2222"
        },
        "5": {
            "Port": "3.1",
            "Value": "31",
            "Timestamp": "2026-02-24T10:45:01Z",
            "UID": "3333"
        },
        "6": {
            "Port": "3.3",
            "Units": "%RH",
            "Value": "33",
            "Timestamp": "2024-11-26T10:45:01Z",
            "Type": "2",
            "Sensor Type": "2",
            "UID": "4444"
        },
        "7": {
            "Port": "3.6.2",
            "Value": "362",
            "Timestamp": "2024-11-26T10:45:01Z",
            "UID": "5555"
        },
        "8": {
            "Port": "3.6.3",
            "Value": "363",
            "Timestamp": "2024-11-26T10:45:01Z",
            "UID": "6666"
        },
        "9": {
            "Port": "3.7.4",
            "Value": "374",
            "Timestamp": "2024-11-26T10:45:01Z",
            "UID": "7777"
        }
    },
    "EXACT Chunks": {
        "0": {
            "Sample ID": "1770393960",
            "Total Chunks": "1",
            "Chunk Number": "1"
        }
    }
}
```

**Entry breakdown:**

| Index | Port | Resolution | Explanation |
|-------|------|-----------|-------------|
| 0 | `3` | Splitter | `Sensor Type = "3"`, UID present, no value |
| 1 | `3.6` | Splitter | `Sensor Type = "3"`, UID present, no value |
| 2 | `3.7` | Splitter | `Sensor Type = "3"`, UID present, no value |
| 3 | `1` | Temperature | No Sensor Type, no Type, no Units → default temperature |
| 4 | `2` | Temperature | No Sensor Type, no Type, no Units → default temperature |
| 5 | `3.1` | Temperature | No Sensor Type, no Type, no Units → default temperature |
| 6 | `3.3` | Humidity (RH) | `Sensor Type = "2"`, `Type = "2"`, `Units = "%RH"` |
| 7 | `3.6.2` | Temperature | No Sensor Type, no Type, no Units → default temperature |
| 8 | `3.6.3` | Temperature | No Sensor Type, no Type, no Units → default temperature |
| 9 | `3.7.4` | Temperature | No Sensor Type, no Type, no Units → default temperature |

### 8.2 LTE path (Coiote shadow update) — multi-chunk

**Chunk 1 of 2:**

```json
{
    "state": {
        "reported": {
            "EXACT Sensor": {
                "0": {
                    "Port": "1",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "3",
                    "Sensor Type": "3",
                    "UID": "593184206a252226"
                },
                "1": {
                    "Port": "1.1",
                    "Units": "cel",
                    "Value": "11.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "Sensor Type": "1",
                    "UID": ""
                },
                "2": {
                    "Port": "1.2",
                    "Units": "cel",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "UID": ""
                },
                "3": {
                    "Port": "4",
                    "Units": "cel",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "Sensor Type": "2",
                    "UID": ""
                },
                "4": {
                    "Port": "4",
                    "Units": "%RH",
                    "Value": "45.65",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "2",
                    "Sensor Type": "2",
                    "UID": ""
                },
                "5": {
                    "Port": "2",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "3",
                    "UID": "569684206a252226"
                },
                "6": {
                    "Port": "2.1",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "3",
                    "UID": "559684206a252226"
                },
                "7": {
                    "Port": "2.1.1",
                    "Units": "cel",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "UID": ""
                }
            },
            "EXACT Chunks": {
                "0": {
                    "Sample ID": "1770391358",
                    "Total Chunks": "2",
                    "Chunk Number": "1"
                }
            }
        }
    }
}
```

**Chunk 2 of 2:**

```json
{
    "state": {
        "reported": {
            "EXACT Sensor": {
                "0": {
                    "Port": "2.1.2",
                    "Units": "cel",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "UID": "549684206a252226"
                },
                "1": {
                    "Port": "2.2",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "3",
                    "UID": "579684206a252226"
                },
                "2": {
                    "Port": "2.2.1",
                    "Units": "",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "UID": ""
                },
                "3": {
                    "Port": "2.2.2",
                    "Units": "",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "UID": ""
                },
                "4": {
                    "Port": "3.1",
                    "Units": "cel",
                    "Value": "11.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "1",
                    "Sensor Type": "2",
                    "UID": ""
                },
                "5": {
                    "Port": "3.1",
                    "Units": "%RH",
                    "Value": "45.65",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "2",
                    "Sensor Type": "2",
                    "UID": ""
                },
                "6": {
                    "Port": "3.2",
                    "Units": "cel",
                    "Value": "12.782276153564453",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "99",
                    "Position": "1",
                    "UID": ""
                },
                "7": {
                    "Port": "3.2",
                    "Units": "cel",
                    "Value": "12.45",
                    "Timestamp": "2024-11-26T10:45:01Z",
                    "Type": "99",
                    "Position": "2",
                    "UID": ""
                }
            },
            "EXACT Chunks": {
                "0": {
                    "Sample ID": "1770391358",
                    "Total Chunks": "2",
                    "Chunk Number": "2"
                }
            }
        }
    }
}
```

### 8.3 Rope sensor example (LoRa path)

A rope sensor on port `3.8` with 4 temperature readings:

```json
{
    "thingName": "urn:dev:mac:129B6A594D679D87",
    "EXACT Sensor": {
        "0": {
            "Port": "3",
            "Sensor Type": "3",
            "UID": "Splitter50"
        },
        "1": {
            "Port": "3.8",
            "Sensor Type": "4",
            "UID": "A1B2C3D4E5F60001"
        },
        "2": {
            "Port": "3.8.1",
            "Value": "21.5",
            "Timestamp": "2026-03-19T10:45:01Z"
        },
        "3": {
            "Port": "3.8.2",
            "Value": "22.1",
            "Timestamp": "2026-03-19T10:45:01Z"
        },
        "4": {
            "Port": "3.8.3",
            "Value": "23.8",
            "Timestamp": "2026-03-19T10:45:01Z"
        },
        "5": {
            "Port": "3.8.4",
            "Value": "24.2",
            "Timestamp": "2026-03-19T10:45:01Z"
        }
    },
    "EXACT Chunks": {
        "0": {
            "Sample ID": "1773940186",
            "Total Chunks": "1",
            "Chunk Number": "1"
        }
    }
}
```

**Entry breakdown:**

| Index | Port | Resolution | Explanation |
|-------|------|-----------|-------------|
| 0 | `3` | Splitter | `Sensor Type = "3"`, UID present, no value |
| 1 | `3.8` | Rope parent | Rope Sensor Type (placeholder `"4"`), UID identifies the rope |
| 2 | `3.8.1` | Temperature | Sub-port of rope parent → position 1 along the rope |
| 3 | `3.8.2` | Temperature | Sub-port of rope parent → position 2 along the rope |
| 4 | `3.8.3` | Temperature | Sub-port of rope parent → position 3 along the rope |
| 5 | `3.8.4` | Temperature | Sub-port of rope parent → position 4 along the rope |

> **Note:** Sensor Type `"4"` is used as a placeholder in this example. The actual Sensor Type for each rope type will be assigned when the hardware is selected. Different rope types may receive different Sensor Type values.

### 8.4 Particulate Matter sensor example (LoRa path)

A Sensirion SEN5x on port `1`, emitting all eight readings. This is the JSON
equivalent of `cbor-validation/samples/storage-pm-sensor.diag`. `Units` is
omitted; each value's unit follows from `Type` (section 6.2).

```json
{
    "thingName": "urn:dev:mac:129B6A594D679D87",
    "EXACT Sensor": {
        "0": {
            "Port": "1",
            "Value": "22.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Sensor Type": "5"
        },
        "1": {
            "Port": "1",
            "Value": "45.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "2",
            "Sensor Type": "5"
        },
        "2": {
            "Port": "1",
            "Value": "5.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "3",
            "Sensor Type": "5"
        },
        "3": {
            "Port": "1",
            "Value": "7.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "4",
            "Sensor Type": "5"
        },
        "4": {
            "Port": "1",
            "Value": "9.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "5",
            "Sensor Type": "5"
        },
        "5": {
            "Port": "1",
            "Value": "10.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "6",
            "Sensor Type": "5"
        },
        "6": {
            "Port": "1",
            "Value": "120.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "7",
            "Sensor Type": "5"
        },
        "7": {
            "Port": "1",
            "Value": "1.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "8",
            "Sensor Type": "5"
        }
    },
    "EXACT Chunks": {
        "0": {
            "Sample ID": "1745262000",
            "Total Chunks": "1",
            "Chunk Number": "1"
        }
    }
}
```

**Entry breakdown:**

| Index | Port | Type | Reading | Unit (derived) |
|-------|------|------|---------|----------------|
| 0 | `1` | — (default 1) | Temperature | cel |
| 1 | `1` | `"2"` | Humidity | %RH |
| 2 | `1` | `"3"` | PM1.0 | ug/m3 |
| 3 | `1` | `"4"` | PM2.5 | ug/m3 |
| 4 | `1` | `"5"` | PM4.0 | ug/m3 |
| 5 | `1` | `"6"` | PM10 | ug/m3 |
| 6 | `1` | `"7"` | VOC Index | idx |
| 7 | `1` | `"8"` | NOx Index | idx |

### 8.5 Air Quality sensor example (LoRa path)

A Bosch BME680 on port `2`, emitting all six readings. JSON equivalent of
`cbor-validation/samples/storage-air-quality.diag`. Pressure is in hPa.

```json
{
    "thingName": "urn:dev:mac:129B6A594D679D87",
    "EXACT Sensor": {
        "0": {
            "Port": "2",
            "Value": "22.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Sensor Type": "6"
        },
        "1": {
            "Port": "2",
            "Value": "1013.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "9",
            "Sensor Type": "6"
        },
        "2": {
            "Port": "2",
            "Value": "48.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "2",
            "Sensor Type": "6"
        },
        "3": {
            "Port": "2",
            "Value": "75.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "10",
            "Sensor Type": "6"
        },
        "4": {
            "Port": "2",
            "Value": "650.0",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "11",
            "Sensor Type": "6"
        },
        "5": {
            "Port": "2",
            "Value": "0.5",
            "Timestamp": "2025-04-21T19:00:00Z",
            "Type": "12",
            "Sensor Type": "6"
        }
    },
    "EXACT Chunks": {
        "0": {
            "Sample ID": "1745262000",
            "Total Chunks": "1",
            "Chunk Number": "1"
        }
    }
}
```

**Entry breakdown:**

| Index | Port | Type | Reading | Unit (derived) |
|-------|------|------|---------|----------------|
| 0 | `2` | — (default 1) | Temperature | cel |
| 1 | `2` | `"9"` | Pressure | hPa |
| 2 | `2` | `"2"` | Humidity | %RH |
| 3 | `2` | `"10"` | IAQ | idx |
| 4 | `2` | `"11"` | CO2 | ppm |
| 5 | `2` | `"12"` | b-VOC | ppm |
