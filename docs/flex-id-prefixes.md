# Flex Device ID Prefixes

Every Flex device ID begins with a two-digit prefix that identifies the product.
The prefix is followed by a six-digit serial number (e.g. `13000623`).

| Prefix | Product | Example ID | Notes |
|--------|---------|------------|-------|
| 10 | Flex Logger | 10000372 | Standard logger with four temperature ports. |
| 12 | Flex Embeddable | 12000372 | Two temperature ports; supports a two-way splitter. |
| 13 | Flex Lite | 13000623 | Two temperature ports; supports a two-way splitter. Previously named Flex Ambient. |
| 14 | Climate Pro Lite | 14000001 | Same hardware as Flex Lite, different product label. |
| 15 | Control Pro Sensor | 15000001 | Same hardware as Flex Lite. |

## Data format for Flex Lite and Flex Embeddable

Flex Lite and Flex Embeddable report data in the following field order on all
channels (BLE and LoRa/Splitter):

```
device_type, sensor_id, sensor_battery, packet_number, reading_time,
port1, port2, port1.B, port2.B, ambient, humidity, reclaimed
```

`port1.B` and `port2.B` are the two splitter sub-ports. They occupy the third and
fourth temperature positions (immediately before `ambient`); there is no separate
trailing splitter block.
