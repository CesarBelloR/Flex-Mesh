# Immediate Report Thresholds — LwM2M API Specification

## 1. Overview

Portal is adding a **Device Threshold** alert type: a user configures up to four
*immediate report thresholds* on a device. When a sensor reading crosses one of
these thresholds, the device wakes its radio and transmits the current sample
immediately — over LoRa or LTE depending on device mode — instead of waiting for
the next scheduled transmission interval. The report carries an alert flag
naming the threshold that breached, and Portal raises the alert from it.

This document specifies the LwM2M API between the server (Coiote / Portal) and
the device: the **EXACT Threshold** object (ID 48944), its write/read flows, and
the device-side trigger semantics. It is the contract for both the firmware
implementation (FW-1178, FW-1179) and the software team.

Related resources:

- Jira: FW-1177 (this spec), FW-1178 (settings), FW-1179 (trigger), epic FW-997
- Portal design: [Figma — Hardware View, Device Threshold](https://www.figma.com/design/TpRRbpnHXob3BdftScKD7O/Hardware---View?node-id=651-1540&p=f)
- Object definition (DDF): `applications/etc-app/src/cloud/lwm2m/exact_objects/exact-threshold_48944.xml`
- Value-type enumeration: `docs/specs/flex2-0_data_rules.md`, section 6.2

## 2. Design Constraints

- **Per value type, not per port.** A threshold applies to all readings of its
  value type across *all* input ports of the device, including hierarchical
  (splitter) ports. This matches the Portal design ("Thresholds are set across
  all input ports").
- **Four slots.** A device holds exactly four threshold slots. Each slot binds
  one value type, one alert type, and one value. Multiple slots may share a
  value type (e.g. a low and a high temperature threshold).
- **Flex 2.0 value types.** The value-type enumeration is the Flex 2.0 one
  (1 = temperature, 2 = humidity, 3–12 environmental). The device accepts the
  full range today, so the API does not change when Flex 2.0 sensors land.
  Current firmware only produces temperature and humidity readings; thresholds
  on other value types are stored but never trigger. Portal restricts its UI to
  temperature and humidity for now.
- **Alert types.** *Exceeds* (reading rises above the value) and *drops below*
  (reading falls below the value).

## 3. Object Definition — EXACT Threshold (48944)

| Property | Value |
|----------|-------|
| Object ID | 48944 |
| Object URN | `urn:oma:lwm2m:x:48944:1.0` |
| Object version | 1.0 |
| Instances | Multiple — the device creates instances 0–3 at boot (one per slot) |
| Instance creation | By the device only; the server must not Create/Delete instances |

### Resources

| RID | Name | Ops | Type | Range | Description |
|-----|------|-----|------|-------|-------------|
| 1 | Enabled | RW | Boolean | | Enables the slot. Disabled slots keep their configuration but are never evaluated. Default `false`. |
| 2 | Value Type | RW | Integer | 1..12 | Flex 2.0 value type the threshold applies to (section 4). |
| 3 | Alert Type | RW | Integer | 0..1 | 0 = Exceeds, 1 = Drops below. |
| 4 | Threshold Value | RW | Float | | Threshold in the default unit of the value type (section 4). |
| 5 | Alert | R | Boolean | | `true` while this slot has a threshold-triggered report awaiting delivery. Set when the slot triggers, cleared once the report carrying it has been sent successfully. |
| 6 | Last Triggered | R | Time | | Timestamp of the slot's last threshold crossing since boot; 0 = never. Troubleshooting only. |
| 7 | Trigger Count | R | Integer | | Threshold crossings recorded by the slot since boot. Troubleshooting only. |

Paths follow the usual scheme: `/48944/<slot>/<rid>`, slot 0–3.

### Write validation

Writes are validated at the CoAP layer (as for EXACT Configuration 48931) and
rejected with 4.00 Bad Request when out of range: Value Type outside 1..12,
Alert Type outside 0..1, or a non-finite Threshold Value. A rejected write
leaves the stored configuration unchanged.

### Persistence and read-back

The configuration resources (RIDs 1–4) persist in non-volatile storage and
survive reboot and power loss. The device re-populates them from stored values
at boot and after any local change, so a Read via Coiote always reflects the
active configuration. The alert and status resources (RIDs 5–7) are volatile —
see below.

### Alert flag (RID 5)

`Alert` identifies the threshold that caused a threshold-triggered report. The
device sets it when the slot triggers, includes it in the report (section 6),
and clears it once that report has been sent successfully. If the transmission
fails, the flag stays set, so the retry still carries it. The flag is volatile:
`false` at boot, never persisted.

Because the device clears the flag after a successful send, a `true` value on
the server describes **the report it arrived with**, not the device's live
state: read it as "this report was triggered by threshold *N*", not as "this
threshold is still out of range". Whether the slot can trigger again is governed
by the re-arm rules in section 5.

### Status resources (troubleshooting only)

Last Triggered (RID 6) and Trigger Count (RID 7) are diagnostic values. They
count threshold *crossings*, not immediate reports: a crossing inside the
rate-limit hold-off window (section 5) updates them without transmitting. The
device does not update them towards the server on its own — they are never
included in LwM2M Sends or notifications; a manual Read returns their current
values. Both reset at boot (Last Triggered = 0, Trigger Count = 0). Portal must
not build alerting on them: alerting uses the Alert flag (RID 5), which the
device sends with the report.

## 4. Value Types and Units

Value types and their units follow the Flex 2.0 enumeration
(`flex2-0_data_rules.md` section 6.2). The Threshold Value is always expressed
in the value type's *default* unit; there is no unit resource. Portal converts
user-facing units (e.g. °F) before writing.

| Value Type | Name | Threshold unit | Triggers today? |
|-----------|------|----------------|-----------------|
| 1 | Temperature | °C | Yes |
| 2 | Humidity | %RH | Yes |
| 3–6 | PM1.0 / PM2.5 / PM4.0 / PM10 | µg/m³ | Not until Flex 2.0 sensors |
| 7, 8 | VOC / NOx Index | index | Not until Flex 2.0 sensors |
| 9 | Pressure | hPa | Not until Flex 2.0 sensors |
| 10 | IAQ | index | Not until Flex 2.0 sensors |
| 11, 12 | CO2 / b-VOC | ppm | Not until Flex 2.0 sensors |

## 5. Trigger Semantics

### Evaluation

The device evaluates thresholds at every sampling cycle (log interval), after
readings are taken. For each enabled slot, every valid reading whose value type
matches the slot is compared against the Threshold Value. Evaluation state is
kept **per slot and per port**, so two ports crossing the same threshold at
different times each produce a report.

### Crossing (edge-triggered)

A slot triggers for a port when the reading is beyond the threshold and the
previous reading on that port was not:

- *Exceeds* (Alert Type 0): reading > value, previous reading ≤ value
- *Drops below* (Alert Type 1): reading < value, previous reading ≥ value

**Arming.** When a slot is (re)configured or enabled, its evaluation state
resets. If the first reading after arming is already beyond the threshold, the
slot triggers immediately — the user is alerted about an already-out-of-range
condition as soon as the threshold reaches the device.

**Reboot.** Evaluation state is kept in RAM only and is not retained across
reboot: after a reboot every port behaves as freshly armed, so a reading still
beyond an enabled threshold triggers one immediate report on the first sampling
cycle after boot.

**Re-arming.** After a trigger, the port re-arms only when a reading returns to
the non-triggering side of the threshold. A value that stays beyond the
threshold does not re-trigger on subsequent cycles.

### Rate limiting

The firmware enforces a minimum interval between threshold-triggered
transmissions (compile-time constant, all slots combined). Crossings during the
hold-off window do not produce an extra transmission; the readings are still
logged and arrive with the next scheduled transmission. This bounds battery and
airtime cost when a reading oscillates around a threshold. A crossing inside the
hold-off window still sets the Alert flag (RID 5) and updates the status
resources (RIDs 6 and 7).

### Invalid readings

Invalid or disconnected sensors produce no readings and are never evaluated. A
port whose sensor disappears loses its evaluation state; when readings return,
the port behaves as freshly armed.

## 6. Immediate Report Behavior

A trigger causes the device to transmit its **current sample** through the
normal transmission path for its mode, ahead of schedule. The sample content is
unchanged — the same readings a scheduled transmission would carry — plus the
alert flag of the threshold(s) that breached.

- **LTE mode**: the device connects (or uses its active session) and performs a
  regular upload via LwM2M Send. The Send additionally includes the `Alert`
  resource (RID 5) of the triggering slot(s), so Portal is told which threshold
  breached. The flag is cleared once the Send is confirmed; slots that did not
  trigger are not included.
- **LoRa mode**: the device schedules an immediate LoRa transmission of the
  current sample using the normal packet format (v1 ASCII today, CBOR after
  Flex 2.0). The relay forwards it like any other packet. **Open item:** the v1
  ASCII format has no field for the alert flag, so LoRa reports are unmarked
  until Flex 2.0 — carrying it requires a record-level key in the Flex 2.0 CBOR
  format (key 23 is the next free one and is proposed for this purpose), a
  companion change to `Flex Storage and transmission Format 2.0.md` tracked
  separately.

Scheduled logging and transmissions continue unchanged; the immediate report is
additional. If the immediate transmission fails, the data is not lost — it is
retained and delivered by the normal retry/scheduled paths, and the alert flag
stays set until one of them succeeds.

## 7. Settings Delivery Latency

Threshold writes reach the device through its LwM2M (Coiote) connection:

- **LTE mode** devices run in queue mode; a write lands at the next check-in
  (bounded by the transmission interval).
- **LoRa mode** loggers only connect over LTE during their daily sync, so new
  thresholds can take up to ~24 h to reach them. Evaluation itself always runs
  locally, so once delivered, triggers fire immediately in either mode.
  Distribution over the LoRa relay path is out of scope for v1.0.

## 8. Server Usage (Coiote)

Coiote addresses resources through the data-model keys from the DDF XML: object
`EXACT Threshold`, resource names as in section 3. The DDF must be uploaded to
the Coiote tenant data model before use.

### Example

The Portal example configuration "alert below 10 °C or above 20 °C" maps to:

| Path | Resource | Value |
|------|----------|-------|
| /48944/0/2 | Value Type | 1 (temperature) |
| /48944/0/3 | Alert Type | 1 (drops below) |
| /48944/0/4 | Threshold Value | 10.0 |
| /48944/0/1 | Enabled | true |
| /48944/1/2 | Value Type | 1 (temperature) |
| /48944/1/3 | Alert Type | 0 (exceeds) |
| /48944/1/4 | Threshold Value | 20.0 |
| /48944/1/1 | Enabled | true |

Slots 2 and 3 remain disabled. Write `Enabled` last (or write all four resources
in one batch): the slot arms atomically when it becomes enabled with a complete
configuration. To remove a threshold, write `Enabled` = false — the remaining
configuration may be left in place.

### Shadow representation

Like sensor data (`flex2-0_data_rules.md` section 2.1), the object appears in
the device's AWS IoT shadow under `state.reported`, keyed by object name with
numbered instance objects and resource-name keys. All values are JSON strings.
The example configuration above, in the report triggered by the 10 °C threshold,
is represented as:

```json
{
    "state": {
        "reported": {
            "EXACT Threshold": {
                "0": {
                    "Enabled": "true",
                    "Value Type": "1",
                    "Alert Type": "1",
                    "Threshold Value": "10.0",
                    "Alert": "true",
                    "Last Triggered": "2026-08-24T14:05:01Z",
                    "Trigger Count": "3"
                },
                "1": {
                    "Enabled": "true",
                    "Value Type": "1",
                    "Alert Type": "0",
                    "Threshold Value": "20.0",
                    "Alert": "false",
                    "Last Triggered": "0",
                    "Trigger Count": "0"
                },
                "2": {
                    "Enabled": "false",
                    "Value Type": "1",
                    "Alert Type": "0",
                    "Threshold Value": "0.0",
                    "Alert": "false",
                    "Last Triggered": "0",
                    "Trigger Count": "0"
                },
                "3": {
                    "Enabled": "false",
                    "Value Type": "1",
                    "Alert Type": "0",
                    "Threshold Value": "0.0",
                    "Alert": "false",
                    "Last Triggered": "0",
                    "Trigger Count": "0"
                }
            }
        }
    }
}
```

Slots 0 and 1 are the low/high temperature thresholds from the example; slots 2
and 3 hold device defaults. `Alert` is `"true"` on slot 0 only: the 10 °C
threshold is the one that triggered this report. The device clears the flag once
the report is delivered, so the `"true"` in the shadow belongs to this report
(section 3) — the next report from this device carries `"false"` unless that
threshold breaches again. `Last Triggered` uses the same ISO 8601 UTC format as
sensor timestamps, `"0"` when the slot has not triggered since boot; because the
troubleshooting resources are read-on-demand (section 3), their shadow values
only change after a manual Read.

The threshold-triggered sample data accompanies this update in the same shadow
document, in the usual `EXACT Sensor` / `EXACT Chunks` form.

## 9. Design Rationale

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Slot model | 4 fixed instances, device-created | Matches the Portal design (max 4 rows); avoids server-side Create/Delete and dynamic instance bookkeeping on the device. |
| Scope | Per value type, all ports | Matches the Portal design; per-port thresholds would multiply configuration size and UI complexity without a current use case. |
| Value types | Flex 2.0 enumeration, full range accepted | Forward compatible: no API or data-model change when Flex 2.0 sensor types ship. |
| Unit handling | Implied by value type | Same convention as Flex 2.0 data rules; avoids a redundant writable resource and unit-mismatch states. |
| Trigger semantics | Edge-triggered with re-arm | "Notified the moment an input crosses it" without repeated reports while a value stays out of range. |
| Rate limit | Compile-time firmware constant | Battery protection is a firmware concern; not user-tunable until a need is shown. |
| Alert flag | Boolean on the threshold slot itself (RID 5) | Portal learns which threshold breached without re-deriving it from the readings, and without a second object to model, upload, and keep in step with the configuration. |
| Alert flag lifetime | Set on trigger, cleared on successful send | The flag exists to mark the report it travels with. Clearing on delivery means a failed transmission retries with the marker intact, and no separate acknowledgement path is needed. |
| Troubleshooting resources | Read-on-demand diagnostics (RIDs 6, 7) | Visibility without extra airtime: not reported proactively, reset at boot. Alerting uses the Alert flag instead. |
