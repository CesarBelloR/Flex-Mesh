---
description: Find the correct USB serial port for the relay device on the Raspberry Pi
allowed-tools: Bash(ssh:*), Read
---

Identify the correct serial port for the ETC Flex relay device connected to the Raspberry Pi.

## Pi Configuration

- Host: `exact-pi@192.168.1.6` (local, default) or `exact-pi@100.70.106.87` (Tailscale)
- SSH key: `~/.ssh/rpi_em3`
- SSH flags: `-o IdentitiesOnly=yes`

## Arguments

`$ARGUMENTS` can contain:
- **Network**: `tailscale` to use the Tailscale IP instead of the local network

## Steps

### 1. Determine host

If `$ARGUMENTS` contains `tailscale`, set `REMOTE=exact-pi@100.70.106.87`, otherwise `REMOTE=exact-pi@192.168.1.6`.

### 2. List USB serial devices

```bash
ssh -i ~/.ssh/rpi_em3 -o IdentitiesOnly=yes $REMOTE \
  "for port in /dev/ttyACM*; do
     echo \"=== \$port ===\"
     udevadm info --query=property --name=\$port 2>/dev/null | grep -E 'ID_SERIAL|ID_MODEL|ID_VENDOR'
   done"
```

### 3. Identify the correct port

Two types of device will be present:

| Vendor   | Model   | Purpose                        |
|----------|---------|--------------------------------|
| SEGGER   | J-Link  | Debugger/programmer (ignore for serial) |
| ZEPHYR   | USB-DEV | ETC Flex CDC UART console      |

The **Zephyr USB-DEV** port is the relay serial console. If there are two Zephyr ports (e.g., `ttyACM2` and `ttyACM3`), pick the **lower-numbered** one — it is the shell/log console.

### 4. Verify the port works

Send a carriage return and read the shell prompt:

```bash
ssh -i ~/.ssh/rpi_em3 -o IdentitiesOnly=yes $REMOTE \
  "python3 -c \"
import serial, time
s = serial.Serial('<PORT>', 115200, timeout=2)
s.write(b'\r\n')
time.sleep(0.5)
s.read(s.in_waiting)
s.write(b'settings get_device\r\n')
time.sleep(1)
print(s.read(s.in_waiting).decode('utf-8', errors='replace'))
s.close()
\""
```

Replace `<PORT>` with the identified port (e.g., `/dev/ttyACM2`).

### 5. Report

Print the identified port and the device mode (0 = relay, 1 = LoRa logger, 2 = LTE logger).
