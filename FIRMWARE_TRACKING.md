# Flex 1 Firmware — Mesh Adaptation Tracking Document

> **Project:** Adapting EXACT Technology Flex 1 Firmware for LoRa Mesh (Heltec V3 Interoperability)  
> **Repository:** [CesarBelloR/Flex-Mesh](https://github.com/CesarBelloR/Flex-Mesh) (upstream: `exact-technology/etc-flex-firmware`)  
> **Target Hardware:** Flex 1 (nRF52840 / Nina-B406 + SX1262 LoRa)  
> **Protocol Target:** EXACT LoRa Mesh v2.1 Protocol (Interoperable with Heltec WiFi LoRa 32 V3 mesh nodes/gateways)  
> **Current Version:** `4.1.1`  
> **Last Updated:** 2026-09-28  

---

## Table of Contents
1. [Overview & Objectives](#overview--objectives)
2. [Radio & Protocol Architecture](#radio--protocol-architecture)
3. [Key Firmware Components Modified](#key-firmware-components-modified)
4. [Changelog & Commits](#changelog--commits)
5. [Current Status & Active Tasks](#current-status--active-tasks)
6. [Testing & Verification Guide](#testing--verification-guide)
7. [Environment & Toolchain Notes](#environment--toolchain-notes)

---

## 1. Overview & Objectives

The goal of this project is to adapt the existing **EXACT Flex 1** sensor node firmware to operate within the **EXACT LoRa Mesh v2.1** network alongside **Heltec WiFi LoRa 32 V3** repeaters and gateways.

### Core Goals:
- **RF Compatibility:** Align physical LoRa modulation (Spreading Factor, Sync Word, Preamble) with Heltec V3 mesh nodes.
- **Binary Frame Interoperability:** Support binary `DataPkt` (0xEA), dynamic parent discovery `JOIN_REQUEST` (0xE0) / `JOIN_OFFER` (0xE1), and `ACK` (0xF3).
- **Dual-Input Sensor Reporting:** Package Input 1 (Temperature) and Input 2 (Temperature/Relative Humidity) along with battery voltage and CRC-16.
- **Failover & Orphan Mode:** Enable transmission in orphan mode when no parent node responds, allowing mesh repeaters to forward packets without blocking.
- **Time Synchronization:** Synchronize on-board RTC with network UTC time received in parent ACKs.
- **System Stability:** Prevent MCUboot image rollback via early self-confirmation on boot.

---

## 2. Radio & Protocol Architecture

### 2.1 RF Physical Layer Configuration (`CONFIG_ETC_LORA_XMESH_PROTOCOL`)
| Parameter | Flex Standard Value | Mesh v2.1 (Heltec V3) Value | Notes |
| :--- | :--- | :--- | :--- |
| **Frequency** | 915.0 MHz (US915) | 915.0 MHz | Configurable via Kconfig |
| **Bandwidth** | 125 kHz (`BW_125_KHZ`) | 125 kHz | Standard channel |
| **Spreading Factor** | `SF7` | `SF9` (`DATARATE_SF9`) | Higher sensitivity / range for mesh |
| **Coding Rate** | `CR_4_5` | `CR_4_5` | Error correction |
| **Sync Word** | `0x12` (Private) | `0x34` (Public / LoRaWAN) | Matched to Heltec mesh sync word |
| **Preamble Length** | 8 symbols | 8 symbols | Standard sync |
| **TX Power** | +14 dBm | +14 dBm | Configurable |

### 2.2 Packet Formats

#### 1. Join Request (`XMESH_PKT_JOIN_REQUEST` = `0xE0`) — 4 Bytes
Used by node to find nearby parents on network wake/startup.
```
[0]: 0xE0 (Type)
[1..2]: origin_id (uint16_t, Little-Endian)
[3]: req_seq (uint8_t, sequence counter)
```

#### 2. Join Offer (`XMESH_PKT_JOIN_OFFER` = `0xE1`) — 9 Bytes
Sent by repeater/parent offering routing to the node.
```
[0]: 0xE1 (Type)
[1..2]: from_id (uint16_t, potential parent)
[3..4]: to_id (uint16_t, must match origin_id)
[5]: parent_id of the offerer
[6..7]: path_cost (uint16_t, estimated link/routing cost)
[8]: hop_count (uint8_t)
```

#### 3. Data Packet (`XMESH_PKT_DATA` = `0xEA`) — 18 Bytes (at origin)
Binary sensor reading broadcast or sent to parent.
```
[0]: 0xEA (Type)
[1..2]: parent_id (uint16_t, 0x0000 if orphan)
[3]: path_len (uint8_t, 0x00 at origin)
[4..5]: origin_id (uint16_t, device ID)
[6..7]: orig_crc16 (uint16_t, CRC-16/CCITT over sensor payload)
[8..11]: sampled_utc_sec (uint32_t, UTC epoch timestamp)
[12..13]: temp_c_x100 (int16_t, Input 1 Temperature * 100)
[14..15]: rh_pct_x100 (uint16_t, Input 2 RH * 100 or Temp * 100)
[16..17]: batt_mv (uint16_t, Battery voltage in mV)
```

#### 4. Acknowledgment (`XMESH_PKT_ACK` = `0xF3`) — 13 Bytes
Parent confirmation carrying network UTC time for clock synchronization.
```
[0]: 0xF3 (Type)
[1..2]: ack_from (uint16_t)
[3..4]: ack_to (uint16_t, must match origin_id)
[5..6]: ack_crc (uint16_t, must match orig_crc16)
[7]: status (uint8_t)
[8]: reserved (uint8_t)
[9..12]: utc_sec (uint32_t, Network UTC epoch timestamp)
```

---

## 3. Key Firmware Components Modified

### 1. `applications/etc-app/src/modules/lora_module.c`
- Added `#if defined(CONFIG_ETC_LORA_XMESH_PROTOCOL)` block.
- Implemented:
  - `xmesh_crc16_ccitt()`: Standard CRC-16 computation over the 6-byte sensor payload.
  - `xmesh_get_origin_id()`: Hardware ID derivation using `hwinfo_get_device_id()`.
  - `xmesh_join_parent()`: Join handshake broadcasting `JOIN_REQUEST` and awaiting `JOIN_OFFER` (timeout 2500ms).
  - `module_lora_process_packet_xmesh()`:
    - Extracts Input 1 (`SENSOR_INPUT_IN1`) and Input 2 (`SENSOR_INPUT_IN2` or `SENSOR_INPUT_HUMID`).
    - Constructs and transmits 18-byte binary `XMESH_PKT_DATA`.
    - Handles orphan fallback (immediate success if parent is 0, no ACK required).
    - Waits up to 3000ms for `XMESH_PKT_ACK`.
    - Updates local RTC clock when receiving valid network UTC timestamp.
    - Manages retry loops and parent invalidation on persistent timeouts.

### 2. `applications/etc-app/src/modules/Kconfig.lora_module`
- Defined `CONFIG_ETC_LORA_XMESH_PROTOCOL` (default `y`).
- Provides clean toggle between legacy JSON/proprietary Flex protocol and Mesh v2.1 binary protocol.

### 3. `applications/etc-app/CMakeLists.txt`
- Bumped version to `4.1.1`.
- Cleaned up build flags: Automatically unsets `-DCONF_FILE="prj.conf"` and routes `DTC_OVERLAY_FILE` to `EXTRA_DTC_OVERLAY_FILE` when invoked by VS Code / nRF Connect extension. This ensures Zephyr board overlay fragments in `boards/` (e.g. `etc_flex_nrf52840.conf`) are properly loaded.

### 4. `applications/etc-app/src/main.c` & `lwm2m_firmware.c`
- Added `lwm2m_firmware_self_confirm()` called early in `main()` to confirm running images in MCUboot, preventing accidental rollback after flashing or FOTA updates.

### 5. `applications/etc-app/child_image/mcuboot.conf`
- Tweaked MCUboot configuration flags to support the updated memory and boot layout.

---

## 4. Changelog & Commits

| Commit | Date | Author | Summary |
| :--- | :--- | :--- | :--- |
| `e874a58e` | 2026-09-28 | Cesar Bello | **Adapting Flex Firmware to mesh**: Implemented Mesh v2.1 packet format, SF9/Sync 0x34 config, dynamic parent join & orphan fallback, CRC-16, RTC sync on ACK, MCUboot self-confirm, and CMake board config discovery fix. |
| — | 2026-09-28 | Cesar Bello | **Git & Toolchain Environment Fix**: Configured Git user credentials (`contact@cesar-bello.com`), resolved `remote-https` toolchain bug, pointed VS Code `git.path` to `/usr/bin/git`. |

---

## 5. Current Status & Active Tasks

### Status
- [x] Protocol structure defined and implemented in `lora_module.c`.
- [x] Kconfig switch added (`CONFIG_ETC_LORA_XMESH_PROTOCOL`).
- [x] MCUboot rollback prevention integrated into `main.c`.
- [x] CMake build file discovery fixed for VS Code / nRF Connect.
- [x] Git repository synced with `origin/main`.

### Active / Next Steps
- [ ] **Hardware RF Validation:** Flash test image to Flex 1 board and verify RF packet emissions on spectrum analyzer / SDR at 915 MHz.
- [ ] **Heltec V3 Gateway Ingestion:** Test transmission in range of a Heltec V3 mesh gateway or repeater:
  - Verify gateway decodes `0xEA` data frame.
  - Verify In1 (temp) and In2 (RH) values in gateway log / backend payload.
- [ ] **Join & ACK Handshake Test:** Confirm node receives `0xE1` Join Offer and `0xF3` ACK with clock timestamp.
- [ ] **Clock Drift & RTC Verification:** Verify `date_time_set_second()` accurately updates internal time without disrupting periodic sample scheduler.
- [ ] **Low-Power Current Audit:** Confirm LoRa radio powers down properly into sleep mode between transmissions and RX timeout windows.

---

## 6. Testing & Verification Guide

### Building with West
To build the application for the Flex 1 target board:
```bash
# From workspace root
cd /Users/cesar/Exact/flex1/etc-firmware
west build -b etc_flex_nrf52840 applications/etc-app -- -DEXTRA_DTC_OVERLAY_FILE="boards/etc_ninab4.overlay"
```

### Flashing with J-Link / nrfjprog
```bash
west flash
# Or via nrfjprog directly:
nrfjprog --program build/zephyr/zephyr.hex --sectorerase --reset
```

### Monitoring Logs (RTT / UART)
```bash
# Connect RTT client to view [XMESH] and [lora] log messages:
JLinkRTTLogger -Device NRF52840_XXAA -RTTChannel 0 log.txt
# Or using JLinkRTTClient:
JLinkRTTClient
```

Expected log outputs on packet send:
```text
[00:00:05.120] <inf> lora: [XMESH] Discovering parents: JOIN_REQUEST seq 1 (origin 0xF1E1)
[00:00:07.625] <dbg> lora: [XMESH] No join offer received; proceeding in orphan mode (parent=0)
[00:00:07.630] <inf> lora: [XMESH] TX DATA: parent=0x0000, origin=0xF1E1, In1=22.50 C, In2=45.20, batt=3310 mV, crc=0xA2C1
[00:00:07.750] <inf> lora: [XMESH] Transmitted as orphan (no ACK expected)
```

---

## 7. Environment & Toolchain Notes

- **Zephyr SDK:** Nordic NCS Toolchain (`/opt/nordic/ncs/toolchains/f8037e9b83`)
- **Git Binary:** Apple Git (`/usr/bin/git`, version 2.54.0) — configured in `.vscode/settings.json` (`"git.path": "/usr/bin/git"`).
- **Git User:** `Cesar Bello <contact@cesar-bello.com>`
- **Tracking File Location:** [etc-firmware/FIRMWARE_TRACKING.md](file:///Users/cesar/Exact/flex1/etc-firmware/FIRMWARE_TRACKING.md)
- **UART Shell Commands Reference:** [etc-firmware/SHELL_COMMANDS.md](file:///Users/cesar/Exact/flex1/etc-firmware/SHELL_COMMANDS.md)
