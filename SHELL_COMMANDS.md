# Flex 1 Firmware — UART Shell Commands Reference

> **Device:** EXACT Technology Flex 1 (nRF52840 / Nina-B406 + SX1262 LoRa)  
> **Firmware:** `applications/etc-app`  
> **UART Shell Settings:** `115200 baud`, `8 data bits`, `no parity`, `1 stop bit` (`115200 8N1`)  
> **Last Updated:** 2026-09-30  
>
> ⚠️ **Maintenance Rule:** Every time a new shell command is added or modified in the firmware, update this document immediately with syntax, options, source locations, and example output.

---

## Quick Navigation

| Command | Subcommands / Args | Description | Source File |
| :--- | :--- | :--- | :--- |
| [`inputs`](#1-inputs--read-all-logger-inputs) | `[read \| sample]` | Read all logger inputs (Ports 1–4, Splitter 1.B–4.B, Ambient, Humidity, Battery) | [`etc_sensor.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_sensor/etc_sensor.c) |
| [`lora send`](#2-lora-send--transmit-actual-logger-readings-over-lora) | `[sample \| flash]` | Transmit live Port 1, Port 2, and Battery readings over LoRa | [`lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c) |
| [`lora monitor`](#3-lora-monitor--real-time-lora-packet-sniffer) | `[seconds] [send]` | Real-time LoRa traffic sniffer (RX & TX) with RSSI/SNR metrics | [`lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c) |
| [`lora status`](#4-lora-status--radio--mesh-state-inspection) | *(none)* | Inspect LoRa radio configuration, frequencies, and mesh parent/origin | [`lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c) |
| [`lora node_id`](#5-lora-node_id--persistent-mesh-node-id-in-nvseeprom) | `[0x<hex> \| <hex> \| <dec> \| auto]` | Get or permanently flash LoRa Mesh Node ID in NVS flash/EEPROM | [`lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c) |
| [`battery`](#6-battery--battery-simulation--testing) | `enable \| disable \| set <pct>` | Battery simulation and test mode | [`etc_battery.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_battery/etc_battery.c) |
| [`date`](#7-date--system-datetime-management) | `get \| set <YYYY-MM-DD HH:MM:SS>` | Get or set RTC date and time | [`etc_date_time.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_date_time/etc_date_time.c) |
| [`record`](#8-record--flash-sample-records-management) | `report \| dump \| clean \| erase` | Inspect and manage historical records stored in flash | [`etc_device_record.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_devices/etc_device_record.c) |
| [`settings`](#9-settings--device-configuration) | `info \| set_device_id \| ...` | View and modify non-volatile settings and device mode | [`etc_settings.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_settings/etc_settings.c) |

---

## Detailed Command Reference

### 1. `inputs` — Read All Logger Inputs
**Source:** [`src/exact/etc_sensor/etc_sensor.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_sensor/etc_sensor.c) & [`src/exact/etc_sensor/etc_sensor.h`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_sensor/etc_sensor.h)

Reads and displays a formatted table containing every hardware input available on the logger. Uses the unified C API `etc_sensor_read_all_inputs(&all_inputs, fresh)`.

#### Syntax
```text
inputs [read | sample]
```

- **`inputs`** or **`inputs read`**: Reads and prints the latest cached sensor readings immediately. Does not re-energize the sensor power supply rail.
- **`inputs sample`**: Runs an immediate hardware acquisition cycle (energizes sensor rail, drives analog switch / multiplexers, queries 1-Wire TMP1826/DS18B20/SHT31 and ADC channels, and reads the battery PMIC) before printing.

#### Output Fields
- **Timestamp:** UTC epoch timestamp (if time sync is active) or seconds since boot (uptime).
- **Battery:** Measured battery voltage (mV), percentage (0–100%), and charge status (Normal, Charging, Charge Complete, Low, Not Installed).
- **Ambient Temp:** Internal on-board ambient temperature sensor (°C).
- **Humidity:** Relative humidity (%) and the physical port where the humidity probe was detected (e.g. Port 1).
- **Primary Inputs (IN1–IN4):** Temperature in °C and probe type (`[Analog (NTC)]`, `[Digital (1-Wire)]`, or `[None]`).
- **Splitter Inputs (IN5–IN8):** Secondary branch temperatures (Ports 1.B, 2.B, 3.B, 4.B) if 2-way splitters are attached.

#### Example
```text
uart:~$ inputs
=================== Logger Inputs ===================
Timestamp    : 1727618400 (UTC)
Battery      : 3845 mV (85%, Normal)
Ambient Temp : 24.15 C
Humidity     : 48.20 % (Port 1)
--- Primary Inputs (Ports 1-4) ---
  IN1 (Port 1)   :  21.40 C  [Analog (NTC)]
  IN2 (Port 2)   :  23.15 C  [Digital (1-Wire)]
  IN3 (Port 3)   : --        [None]
  IN4 (Port 4)   : --        [None]
--- Splitter Inputs (Ports 1.B-4.B) ---
  IN5 (Port 1.B) : --        [None]
  IN6 (Port 2.B) : --        [None]
  IN7 (Port 3.B) : --        [None]
  IN8 (Port 4.B) : --        [None]
=====================================================
```

---

### 2. `lora send` — Transmit Actual Logger Readings over LoRa
**Source:** [`src/modules/lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c)

Packages the **actual measured Port 1 (`IN1`) and Port 2 (`IN2`) readings**, along with real battery voltage and timestamp, and transmits them immediately over the LoRa network. Displays real-time transmission and ACK progress.

#### Syntax
```text
lora send [sample | flash]
```

- **`lora send`**: Gathers the current readings read from Port 1 and Port 2 and transmits them immediately. (If no prior readings exist since boot, automatically takes a fresh sample first).
- **`lora send sample`**: Forces an immediate fresh hardware acquisition of all inputs before packaging and sending the LoRa packet.
- **`lora send flash`**: Transmits any historical un-ACKed record pending in internal flash memory.

#### Example
```text
uart:~$ lora send
[LoRa] Transmitting actual logger readings:
  Port 1 (IN1) : 21.40 C [Analog (NTC)]
  Port 2 (IN2) : 23.15 C [Digital (1-Wire)]
  Battery      : 3845 mV (3.85 V, 85%, Normal)
[LoRa TX] Packet sent (18 bytes)
[LoRa RX] Captured packet (6 bytes, RSSI: -62 dBm, SNR: 9 dB):
  Type       : XMESH ACK (0xF3)
  Target Node: 0x002A
  Parent Node: 0x0001
  Status     : ACK MATCHED (Confirmed by Parent 0x0001)
[LoRa] Packet transmitted and ACKed successfully.
```

---

### 3. `lora monitor` — Real-Time LoRa Packet Sniffer
**Source:** [`src/modules/lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c)

Turns the UART console into a real-time LoRa packet monitor for a configurable duration. Captures and decodes all packets on the frequency channel, printing packet type, origin, parent, target, RSSI, and SNR.

#### Syntax
```text
lora monitor [seconds] [send]
```

- **`lora monitor`**: Listens for incoming LoRa traffic for **30 seconds** (default).
- **`lora monitor <seconds>`**: Listens for a custom duration in seconds (e.g. `lora monitor 60`).
- **`lora monitor <seconds> send`**: Triggers a live transmission of the real logger inputs first, then listens for the ACK and ongoing channel traffic for the specified duration.

#### Decoded Packet Types
- **`XMESH DATA (0xEA)`**: 18-byte mesh data frame with origin ID, parent ID, CRC-16, UTC timestamp, Port 1 temp, Port 2 temp/RH, and battery voltage.
- **`XMESH ACK (0xF3)`**: 6-byte acknowledgment from parent node, confirming reception and providing network time sync.
- **`XMESH JOIN_REQUEST (0xE0)` / `JOIN_OFFER (0xE1)`**: Dynamic parent discovery handshake frames.
- **Legacy CAPE Packets**: Legacy encrypted ASCII protocol frames.

#### Example
```text
uart:~$ lora monitor 30 send
==========================================================
   LoRa Live Monitor Started (30 seconds)
   RX Freq : 915 MHz | BW: 125 kHz
   Protocol: EXACT LoRa Mesh v2.1 (SF9, Public Sync 0x34)
   Origin  : 0x002A | Parent: 0x0001
==========================================================
--> Triggering live transmission...
[LoRa] Transmitting actual logger readings:
  Port 1 (IN1) : 21.40 C [Analog (NTC)]
  Port 2 (IN2) : 23.15 C [Digital (1-Wire)]
  Battery      : 3845 mV (3.85 V, 85%, Normal)
[LoRa TX] Packet sent (18 bytes)

[+0.8s] [LoRa RX] Captured packet (6 bytes, RSSI: -64 dBm, SNR: 8 dB):
  Type       : XMESH ACK (0xF3)
  Target Node: 0x002A
  Parent Node: 0x0001
  Status     : ACK MATCHED (Confirmed by Parent 0x0001)
[LoRa] Packet transmitted and ACKed successfully.
--> Now listening for channel traffic...

[+5.2s] [LoRa RX] Captured packet (18 bytes, RSSI: -78 dBm, SNR: 6 dB):
  Type       : XMESH DATA (0xEA)
  Origin Node: 0x001B
  Parent Node: 0x0001
  Timestamp  : 1727618405
```

---

### 4. `lora status` — Radio & Mesh State Inspection
**Source:** [`src/modules/lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c)

Displays the complete hardware configuration of the SX1262 LoRa transceiver, active RF parameters, mesh routing parameters, and transmission timers.

#### Syntax
```text
lora status
```

#### Example
```text
uart:~$ lora status
================== LoRa Subsystem Status ==================
Device Ready    : YES (sx1262@0)
Device Mode     : LOGGER
TX Frequency    : 915 MHz
RX Frequency    : 915 MHz
Protocol        : EXACT LoRa Mesh v2.1 (Heltec V3 compatible)
Modulation      : SF9, BW 125 kHz, CR 4/5, Public Sync 0x34
Origin Node ID  : 0x002A
Adopted Parent  : 0x0001 (ADOPTED)
TX Delay        : 0 ms
TX Interval     : 300 s (5 mins)
===========================================================
```

---

### 5. `lora node_id` — Persistent Mesh Node ID in NVS/EEPROM
**Source:** [`src/modules/lora_module.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/modules/lora_module.c) & [`src/exact/etc_devices/etc_device.h`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_devices/etc_device.h)

Displays or permanently flashes the 16-bit Origin Node ID into internal Non-Volatile Storage (NVS flash/EEPROM). The stored ID persists across power cuts, reboots, and firmware flashes.

#### Syntax
```text
lora node_id [0x<hex> | <hex> | <dec> | auto]
```

- **`lora node_id`**: Displays the active Origin Node ID and whether it is persistent in NVS or automatically derived from the Nordic hardware unique silicon ID.
- **`lora node_id 6CE0`** (or `lora node_id 0x6CE0`): Flashes `0x6CE0` (27872) permanently to NVS flash/EEPROM. Accepts both bare hex (`6CE0`) and prefixed (`0x6CE0`). Node IDs are 16-bit unsigned integers represented as 4 hex characters (2 bytes on air).
- **`lora node_id 0x0005`** (or `lora node_id 5`): Flashes `0x0005` permanently to NVS flash/EEPROM. It takes effect immediately on all subsequent LoRa transmissions and persists forever.
- **`lora node_id auto`** (or `lora node_id clear`): Erases the NVS override and restores the factory Nordic hardware ID.

> **Note on "4 bytes" vs "4 hex digits":**  
> An ID such as `6CE0` is 4 characters / 4 hexadecimal digits (nibbles: `6`, `C`, `E`, `0`), which equals **16 bits (2 bytes)**. In the xMesh binary radio packet, it is transmitted in Little-Endian format (`0xE0 0x6C`). If typed with the letter `O` instead of digit `0` (e.g. `6CEO`), the firmware automatically corrects it to `0x6CE0`.

#### Examples
```text
uart:~$ lora node_id
LoRa Mesh Origin Node ID: 0x002A (42) [Auto from Nordic HW ID]

uart:~$ lora node_id 6CE0
[OK] Node ID permanently saved to NVS Flash/EEPROM: 0x6CE0 (27872)
Active Origin Node ID is now: 0x6CE0

uart:~$ lora node_id 0x0005
[OK] Node ID permanently saved to NVS Flash/EEPROM: 0x0005 (5)
Active Origin Node ID is now: 0x0005

uart:~$ lora node_id auto
NVS override cleared. Restored auto hardware Node ID: 0x002A
```

---

### 6. `battery` — Battery Simulation & Testing
**Source:** [`src/exact/etc_battery/etc_battery.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_battery/etc_battery.c)

Allows manual override and simulation of battery percentage for testing low-power, shutdown, and charging alert thresholds.

#### Syntax
```text
battery enable
battery disable
battery set <percentage>
```

- **`battery enable`**: Enables the battery shell override mode.
- **`battery disable`**: Disables the override and resumes live PMIC / ADC sampling.
- **`battery set <0..100>`**: Sets the simulated battery percentage (e.g. `battery set 15`).

---

### 6. `date` — System Date/Time Management
**Source:** [`src/exact/etc_date_time/etc_date_time.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_date_time/etc_date_time.c)

Queries or manually sets the system date and UTC epoch time.

#### Syntax
```text
date get
date set <YYYY-MM-DD hh:mm:ss>
```

- **`date get`**: Prints the current UTC timestamp and human-readable date.
- **`date set 2026-09-30 14:00:00`**: Sets the system date and RTC.

---

### 7. `record` — Flash Sample Records Management
**Source:** [`src/exact/etc_devices/etc_device_record.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_devices/etc_device_record.c)

Inspects and manages sample records stored in the on-board flash memory.

#### Syntax
```text
record report
record dump
record clean
record erase
```

- **`record report`**: Reports total records, ACKed records, and pending NACK records.
- **`record dump`**: Hexdumps the current record in retained RAM.
- **`record erase`**: Clears all records from flash memory.

---

### 8. `settings` — Device Configuration
**Source:** [`src/exact/etc_settings/etc_settings.c`](file:///Users/cesar/Exact/flex1/etc-firmware/applications/etc-app/src/exact/etc_settings/etc_settings.c)

Inspects or configures persistent settings (Device ID, Serial Number, Device Mode, etc.).

#### Syntax
```text
settings info
settings set_device_id <id>
settings set_device <mode>
```

- **`settings info`**: Displays the active device settings, hardware version, firmware version, and device type.
