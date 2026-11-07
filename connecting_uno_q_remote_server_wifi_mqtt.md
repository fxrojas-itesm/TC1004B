# Connecting the Arduino Q to a remote server via WiFi and MQTT

This guide covers connecting the Arduino UNO Q's Qualcomm Dragonwing processor to a local or remote network via Wi-Fi, establishing OpenSSH access, and setting up bi-directional MQTT communication with an external broker/server.

---

## Part 1: Wireless IP Networking (Wi-Fi & SSH without App Lab)

The Dragonwing Debian OS uses standard **NetworkManager** (`nmcli`) and **OpenSSH**. You do not need the proprietary Arduino App Lab software.

### Step 1: Connect to Wi-Fi via `nmcli`

Run these commands from your PC/WSL terminal over your initial SSH connection (`arduino@192.168.7.1` via Ethernet-over-USB):

1. **Scan available Wi-Fi networks:**
   ```bash
   ssh arduino@192.168.7.1 "nmcli dev wifi"
   ```

2. **Connect to your Wi-Fi network:**
   ```bash
   ssh arduino@192.168.7.1 "sudo nmcli dev wifi connect 'YOUR_WIFI_SSID' password 'YOUR_WIFI_PASSWORD'"
   ```

3. **Verify the connection and inspect the board's IP address:**
   ```bash
   ssh arduino@192.168.7.1 "ip addr show wlan0"
   ```
   Look for the `inet` line (e.g., `inet 192.168.1.125/24`).

### Step 2: Update Password or Verify SSH Services Over SSH

If you need to update the `arduino` user password or verify that `ssh` and `avahi-daemon` (`uno-q.local`) are enabled:

1. **Set or update the password for the `arduino` user:**
   ```bash
   ssh arduino@192.168.7.1 "echo -e 'YOUR_CHOSEN_PASSWORD\nYOUR_CHOSEN_PASSWORD' | sudo /usr/local/bin/arduino-passwd"
   ```

2. **Ensure the SSH server is enabled and running:**
   ```bash
   ssh arduino@192.168.7.1 "sudo systemctl enable --now ssh"
   ```

3. **Ensure mDNS (Avahi) discovery is enabled:**
   ```bash
   ssh arduino@192.168.7.1 "sudo systemctl enable --now avahi-daemon"
   ```
   *(This allows addressing the board as `uno-q.local` over Wi-Fi instead of tracking dynamic IP addresses).*

### Standalone Hotspot Mode (No Router)

If you are away from a router, turn the Arduino UNO Q into its own standalone Wi-Fi Access Point:

```bash
ssh arduino@192.168.7.1 "sudo nmcli dev wifi hotspot ifname wlan0 ssid 'Arduino-Uno-Q' password 'arduino123'"
```
Join the `Arduino-Uno-Q` Wi-Fi network from your PC and connect via SSH to `arduino@10.42.0.1`.

---

## Part 2: MQTT Architecture & Broker Setup

In this dual-processor architecture:
* **Microcontroller (STM32U585):** Handles physical hardware pins, sensors, and actuators via C++.
* **Application Processor (Dragonwing QRB2210 Linux):** Connects to the local network over Wi-Fi and handles MQTT pub/sub messaging via Python `paho-mqtt`.
* **RPC Bridge (`arduino_rpc` / `Arduino_RouterBridge`):** Connects the Python process to the C++ sketch over the internal UART bridge (`/dev/ttyHS1`).

### 1. Broker Setup (Remote Server or PC)

1. **Install Mosquitto Broker:**
   - On Linux/Debian: `sudo apt install mosquitto mosquitto-clients`
   - On Windows: Install Eclipse Mosquitto or run via Docker.
2. **Configure `mosquitto.conf`:**
   ```text
   listener 1883 0.0.0.0
   allow_anonymous true
   ```
3. Ensure port `1883` is open in the server firewall.

### 2. Client Setup on the Arduino UNO Q

SSH into the board (`ssh arduino@uno-q.local` or `ssh arduino@<BOARD_IP>`) and install the Python MQTT client:

```bash
pip3 install paho-mqtt
```

### 3. Python MQTT Bridge Script (`mqtt_bridge.py`)

Create a script on the board that bridges incoming MQTT commands to the STM32 microcontroller and publishes hardware sensor events to the remote broker:

```python
import time
import paho.mqtt.client as mqtt
from arduino_rpc import Bridge

BROKER_IP = "192.168.1.50"  # Remote Server or PC IP
BROKER_PORT = 1883

mqtt_client = None

def on_hardware_event(data):
    """Triggered by the STM32 MCU via Bridge.notify()"""
    print(f"[MCU -> MQTT] Event: {data}")
    if mqtt_client:
        mqtt_client.publish("uno_q/telemetry", str(data))

def on_mqtt_message(client, userdata, msg):
    """Triggered when a message arrives from the remote MQTT broker"""
    payload = msg.payload.decode("utf-8")
    print(f"[MQTT -> MCU] Command on {msg.topic}: {payload}")
    Bridge.call("handle_command", payload)

def main():
    global mqtt_client

    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_message = on_mqtt_message
    mqtt_client.connect(BROKER_IP, BROKER_PORT, 60)
    mqtt_client.subscribe("uno_q/commands")
    mqtt_client.loop_start()

    # Register callback for events from STM32 C++ sketch
    Bridge.provide("hardware_event", on_hardware_event)

    print("MQTT Bridge running. Press Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()

if __name__ == "__main__":
    main()
```
