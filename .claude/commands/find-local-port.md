---
description: Find the correct local USB serial port for the ETC Flex device
allowed-tools: Bash(udevadm:*), Bash(python3:*), Bash(ls:*), Read
---

Identify the correct local serial port for the ETC Flex device connected via USB.

## Steps

### 1. List USB serial devices

```bash
for port in /dev/ttyACM*; do
  echo "=== $port ==="
  udevadm info --query=property --name=$port 2>/dev/null | grep -E 'ID_SERIAL|ID_MODEL|ID_VENDOR'
done
```

### 2. Identify the correct port

Two types of device will be present:

| Vendor   | Model   | Purpose                        |
|----------|---------|--------------------------------|
| SEGGER   | J-Link  | Debugger/programmer (ignore for serial) |
| ZEPHYR   | USB-DEV | ETC Flex CDC UART console      |

The **Zephyr USB-DEV** port is the device serial console. If there are two Zephyr ports (e.g., `ttyACM0` and `ttyACM1`), pick the **lower-numbered** one — it is the shell/log console.

### 3. Verify the port works

Send a carriage return and read the shell prompt:

```bash
python3 -c "
import serial, time
s = serial.Serial('<PORT>', 115200, timeout=2)
s.write(b'\r\n')
time.sleep(0.5)
s.read(s.in_waiting)
s.write(b'settings get_device\r\n')
time.sleep(1)
print(s.read(s.in_waiting).decode('utf-8', errors='replace'))
s.close()
"
```

Replace `<PORT>` with the identified port (e.g., `/dev/ttyACM0`).

### 4. Report

Print the identified port and the device mode (0 = relay, 1 = LoRa logger, 2 = LTE logger).
