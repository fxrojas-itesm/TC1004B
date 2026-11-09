Workflow

1. [00Overview](#overview)
2. [01Project layout](#layout)
3. [02Bridge library](#library)
4. [03MCU sketch](#sketch)
5. [04MPU Python script](#python)
6. [05VSCode tasks](#vscode)
7. [06Build, flash, monitor](#run)
8. [07Expected output](#verify)
9. [08Troubleshooting](#trouble)

Arduino UNO QSTM32U585 + QRB2210arduino:zephyr

# Blink D13 every 500 ms and *watch it from Linux*

The STM32 toggles the LED on D13 and sends each new state through the RPC Bridge with `Bridge.notify()`. A Python script on the board's Linux side receives those messages from `arduino-router` and prints them in a terminal. Everything is driven from VSCode.

## 00Overview

Two programs run on the same board. The sketch owns the pin and the timing. The Python script only listens. The dashed path below runs once per code change; the solid path runs every 500 ms while the board is powered.

Build and flash go through SSH and the board's own OpenOCD. Status messages go STM32 → internal UART → `arduino-router` → Unix socket → Python.

D13 over the first 3 seconds. Each edge is followed by one `Bridge.notify("led_state", …)` (filled dot = ON, hollow = OFF), so the Python side sees one line every 500 ms.

### What this guide assumes is already done

- **Toolchain**`arduino-cli` is on your `PATH` with the `arduino:zephyr` core installed (lib index refreshed).
- **Network**The board answers SSH with key authentication at `arduino@192.168.7.1` (Ethernet-over-USB) or `arduino@uno-q.local` (Wi-Fi).
- **Loader**The STM32's Zephyr loader matches your installed core. Refresh it once after any core update, as in your setup guide.
- **Editor**VSCode is open on your workspace folder, the workspace is trusted, and `ssh`/`scp` run from its terminal.

## 01Create the project layout

In VSCode's Explorer, create these folders and files inside your workspace. The sketch folder and the `.ino` must share the same name. The Python script lives outside the sketch folder so `arduino-cli` never tries to compile it.

Workspace

```
<your-workspace>/
├── .vscode/
│   ├── settings.json        board address used by the tasks
│   └── tasks.json           compile · upload · monitor
├── led_bridge/              sketch folder (same name as the .ino)
│   ├── led_bridge.ino       runs on the STM32 (MCU)
│   └── build/               created by arduino-cli
└── mpu/
    └── led_monitor.py       runs on Linux (MPU)
```

## 02Confirm the Bridge library is installed

The sketch includes `Arduino_RouterBridge.h`. Installing the `arduino:zephyr` core normally pulls it in, but check before the first build. Run this in VSCode's terminal.

Terminalyour computer

```bash
arduino-cli lib list | grep -i routerbridge      # macOS / Linux / WSL
arduino-cli lib list | findstr /i routerbridge   # Windows (cmd / PowerShell)

# Only if it is missing (also installs its dependencies):
arduino-cli lib install Arduino_RouterBridge
```

## 03Write the MCU sketch

The loop flips the LED, tells Linux about it, then waits 500 ms. `Bridge.notify()` is fire-and-forget: the sketch never waits for Python, so the blink keeps its timing even when no script is listening.

led_bridge/led_bridge.inoruns on the MCU

```cpp
// led_bridge.ino - runs on the UNO Q's microcontroller (MCU, STM32U585).
// Toggles the LED on D13 every 500 ms and reports each new state to the
// Linux side (MPU) through the RPC Bridge.

#include <Arduino_RouterBridge.h>

const int LED_PIN = 13;                    // D13 (PB13) - also the on-board LED
const unsigned long BLINK_PERIOD_MS = 500;

bool ledOn = false;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Bridge.begin();                          // open the RPC link to arduino-router (waits for Linux)
}

void loop() {
  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);

  // Fire-and-forget message to whoever registered "led_state" on the MPU:
  //   param 0 -> new LED state (true = ON)
  //   param 1 -> MCU uptime in ms, so the Python side can show the real period
  Bridge.notify("led_state", ledOn, millis());

  delay(BLINK_PERIOD_MS);
}
```

- `Bridge.begin()` blocks until `arduino-router` answers on the Linux side. Until then the LED stays off.
- The second parameter, `millis()`, lets the Python script print the period the MCU actually produced.
- The method name `"led_state"` is the contract between both programs. It must be spelled the same on both sides.

## 04Write the MPU Python script

The script talks to `arduino-router` directly over its Unix socket using MessagePack-RPC, so it runs with the board's stock `python3`. It uses `python3-msgpack` when the board has it and falls back to a small built-in codec otherwise, so there is nothing to install.

mpu/led_monitor.pyruns on the MPU

```python
#!/usr/bin/env python3
"""
led_monitor.py - runs on the UNO Q's Linux side (MPU, Dragonwing QRB2210).

Connects to the arduino-router daemon, registers the RPC method "led_state"
and prints every state change that the STM32 sketch sends with
    Bridge.notify("led_state", ledOn, millis());

Run it on the board:   python3 -u led_monitor.py
Stop it with Ctrl+C.
"""
import datetime
import socket
import struct
import sys

SOCKET_PATH = "/var/run/arduino-router.sock"   # arduino-router's Unix socket (on the board)
METHOD = "led_state"                            # must match the name used in the sketch

REQUEST, RESPONSE, NOTIFY = 0, 1, 2             # MessagePack-RPC message types


# --------------------------------------------------------------------------
# MessagePack codec: use python3-msgpack when installed, otherwise a tiny
# built-in codec that covers everything this program sends and receives.
# --------------------------------------------------------------------------
try:
    import msgpack

    def pack(obj):
        return msgpack.packb(obj, use_bin_type=True)

    class Decoder:
        def __init__(self):
            self._u = msgpack.Unpacker(raw=False)

        def feed(self, data):
            self._u.feed(data)

        def __iter__(self):
            return iter(self._u)

except ImportError:

    def pack(obj):
        if obj is None:
            return b"\xc0"
        if obj is True:
            return b"\xc3"
        if obj is False:
            return b"\xc2"
        if isinstance(obj, int):
            if 0 <= obj < 0x80:
                return struct.pack("B", obj)
            if -32 <= obj < 0:
                return struct.pack("b", obj)
            if obj >= 0:
                return b"\xcf" + struct.pack(">Q", obj)
            return b"\xd3" + struct.pack(">q", obj)
        if isinstance(obj, float):
            return b"\xcb" + struct.pack(">d", obj)
        if isinstance(obj, str):
            raw = obj.encode("utf-8")
            n = len(raw)
            if n < 32:
                return struct.pack("B", 0xA0 | n) + raw
            return b"\xdb" + struct.pack(">I", n) + raw
        if isinstance(obj, (list, tuple)):
            n = len(obj)
            head = struct.pack("B", 0x90 | n) if n < 16 else b"\xdd" + struct.pack(">I", n)
            return head + b"".join(pack(x) for x in obj)
        raise TypeError("cannot pack %r" % type(obj))

    class _NeedMore(Exception):
        pass

    class Decoder:
        _FIXED = {  # type byte -> (struct format, size)
            0xCA: (">f", 4), 0xCB: (">d", 8),
            0xCC: (">B", 1), 0xCD: (">H", 2), 0xCE: (">I", 4), 0xCF: (">Q", 8),
            0xD0: (">b", 1), 0xD1: (">h", 2), 0xD2: (">i", 4), 0xD3: (">q", 8),
        }

        def __init__(self):
            self._buf = b""

        def feed(self, data):
            self._buf += data

        def __iter__(self):
            while True:
                try:
                    obj, pos = self._read(0)
                except _NeedMore:
                    return
                self._buf = self._buf[pos:]
                yield obj

        def _take(self, pos, n):
            if pos + n > len(self._buf):
                raise _NeedMore
            return self._buf[pos:pos + n], pos + n

        def _read(self, pos):
            b, pos = self._take(pos, 1)
            t = b[0]
            if t <= 0x7F:
                return t, pos
            if t >= 0xE0:
                return t - 0x100, pos
            if 0xA0 <= t <= 0xBF:
                return self._str(pos, t & 0x1F)
            if 0x90 <= t <= 0x9F:
                return self._array(pos, t & 0x0F)
            if 0x80 <= t <= 0x8F:
                return self._map(pos, t & 0x0F)
            if t == 0xC0:
                return None, pos
            if t == 0xC2:
                return False, pos
            if t == 0xC3:
                return True, pos
            if t in self._FIXED:
                fmt, size = self._FIXED[t]
                raw, pos = self._take(pos, size)
                return struct.unpack(fmt, raw)[0], pos
            if t in (0xD9, 0xDA, 0xDB, 0xC4, 0xC5, 0xC6):        # str8/16/32, bin8/16/32
                size = {0xD9: 1, 0xDA: 2, 0xDB: 4, 0xC4: 1, 0xC5: 2, 0xC6: 4}[t]
                raw, pos = self._take(pos, size)
                n = int.from_bytes(raw, "big")
                if t >= 0xD9:
                    return self._str(pos, n)
                return self._take(pos, n)
            if t in (0xDC, 0xDD):                                  # array16/32
                raw, pos = self._take(pos, 2 if t == 0xDC else 4)
                return self._array(pos, int.from_bytes(raw, "big"))
            if t in (0xDE, 0xDF):                                  # map16/32
                raw, pos = self._take(pos, 2 if t == 0xDE else 4)
                return self._map(pos, int.from_bytes(raw, "big"))
            raise ValueError("unsupported MessagePack type 0x%02x" % t)

        def _str(self, pos, n):
            raw, pos = self._take(pos, n)
            return raw.decode("utf-8", "replace"), pos

        def _array(self, pos, n):
            out = []
            for _ in range(n):
                item, pos = self._read(pos)
                out.append(item)
            return out, pos

        def _map(self, pos, n):
            out = {}
            for _ in range(n):
                k, pos = self._read(pos)
                v, pos = self._read(pos)
                out[k] = v
            return out, pos


# --------------------------------------------------------------------------
# LED state printer
# --------------------------------------------------------------------------
last_mcu_ms = None


def on_led_state(params):
    """Called for every Bridge.notify("led_state", ledOn, millis()) from the MCU."""
    global last_mcu_ms
    state = bool(params[0]) if len(params) > 0 else None
    mcu_ms = params[1] if len(params) > 1 else None

    now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    label = "ON " if state else "OFF"
    delta = ""
    if mcu_ms is not None and last_mcu_ms is not None:
        delta = "  (+%d ms)" % ((mcu_ms - last_mcu_ms) & 0xFFFFFFFF)
    last_mcu_ms = mcu_ms
    print("%s  D13 %s  mcu t=%s ms%s" % (now, label, mcu_ms, delta), flush=True)


def main():
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(SOCKET_PATH)
    except PermissionError:
        sys.exit("Permission denied on %s - try: sudo python3 -u led_monitor.py" % SOCKET_PATH)
    except (FileNotFoundError, ConnectionRefusedError):
        sys.exit("arduino-router is not running - try: sudo systemctl restart arduino-router")
    print("Connected to arduino-router (%s)" % SOCKET_PATH, flush=True)

    # Ask the router to send every "led_state" message to this process.
    REGISTER_ID = 1
    sock.sendall(pack([REQUEST, REGISTER_ID, "$/register", [METHOD]]))

    decoder = Decoder()
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                sys.exit("arduino-router closed the connection.")
            decoder.feed(data)
            for msg in decoder:
                if not isinstance(msg, list) or not msg:
                    continue
                kind = msg[0]

                if kind == RESPONSE and msg[1] == REGISTER_ID:
                    if msg[2]:
                        sys.exit("Could not register '%s': %s" % (METHOD, msg[2]))
                    print("Registered '%s' - waiting for the MCU... (Ctrl+C to stop)" % METHOD,
                          flush=True)

                elif kind == NOTIFY and msg[1] == METHOD:          # [2, method, params]
                    on_led_state(msg[2])

                elif kind == REQUEST:                              # [0, id, method, params]
                    msgid, method, params = msg[1], msg[2], msg[3]
                    if method == METHOD:
                        on_led_state(params)
                        sock.sendall(pack([RESPONSE, msgid, None, True]))
                    else:
                        sock.sendall(pack([RESPONSE, msgid, "unknown method " + str(method), None]))
    except KeyboardInterrupt:
        print("\nStopping.", flush=True)
    finally:
        sock.close()  # the router drops this client's routes when the socket closes


if __name__ == "__main__":
    main()
```

### The three messages on the socket

| Direction | Message | Meaning |
| --- | --- | --- |
| Script → router | `[0, 1, "$/register", ["led_state"]]` | Request: send every `led_state` message to this connection. |
| Router → script | `[1, 1, null, true]` | Response to request 1: registration accepted. |
| Router → script | `[2, "led_state", [true, 48211]]` | Notification forwarded from the sketch's `Bridge.notify()`. |

When the script exits, its socket closes and the router drops the `led_state` route, so you can stop and restart the script at any time without touching the MCU.

## 05Add the VSCode configuration

Put the board address in one place. Change it to `arduino@uno-q.local` if you work over Wi-Fi.

.vscode/settings.json

```json
{
    // Board address used by the tasks in tasks.json.
    // Ethernet-over-USB: "arduino@192.168.7.1"    Wi-Fi: "arduino@uno-q.local"
    "unoq.boardHost": "arduino@192.168.7.1"
}
```

VSCode underlines `unoq.boardHost` as an unknown setting. That warning is harmless: the tasks read it through `${config:unoq.boardHost}`.

The tasks call `arduino-cli`, `ssh` and `scp` directly (`"type": "process"`) and chain steps with `dependsOn` instead of `&&`. Nothing passes through a shell on your computer, so the same file works in WSL, Linux, macOS and native Windows. All paths are built from `${workspaceFolder}`; the only absolute paths are the board's own (`/opt/openocd`, `/tmp/remoteocd`).

.vscode/tasks.json

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "UNO Q: Compile Sketch (MCU)",
            "type": "process",
            "command": "arduino-cli",
            "args": [
                "compile",
                "-b", "arduino:zephyr:unoq",
                "--output-dir", "${workspaceFolder}/led_bridge/build",
                "${workspaceFolder}/led_bridge"
            ],
            "group": { "kind": "build", "isDefault": true },
            "presentation": { "reveal": "always", "panel": "shared", "clear": true },
            "problemMatcher": []
        },
        {
            "label": "UNO Q: Prepare Board Staging Folder",
            "type": "process",
            "command": "ssh",
            "args": ["${config:unoq.boardHost}", "mkdir -p /tmp/remoteocd"],
            "presentation": { "reveal": "always", "panel": "shared" },
            "problemMatcher": [],
            "hide": true
        },
        {
            "label": "UNO Q: Copy Sketch Payload",
            "type": "process",
            "command": "scp",
            "args": [
                "${workspaceFolder}/led_bridge/build/led_bridge.ino.elf-zsk.bin",
                "${config:unoq.boardHost}:/tmp/remoteocd/sketch.bin"
            ],
            "presentation": { "reveal": "always", "panel": "shared" },
            "problemMatcher": [],
            "hide": true
        },
        {
            "label": "UNO Q: Upload Sketch (MCU)",
            "type": "process",
            "command": "ssh",
            "args": [
                "${config:unoq.boardHost}",
                "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
            ],
            "dependsOrder": "sequence",
            "dependsOn": [
                "UNO Q: Compile Sketch (MCU)",
                "UNO Q: Prepare Board Staging Folder",
                "UNO Q: Copy Sketch Payload"
            ],
            "group": "test",
            "presentation": { "reveal": "always", "panel": "shared" },
            "problemMatcher": []
        },
        {
            "label": "UNO Q: Copy Python Script",
            "type": "process",
            "command": "scp",
            "args": [
                "${workspaceFolder}/mpu/led_monitor.py",
                "${config:unoq.boardHost}:led_monitor.py"
            ],
            "presentation": { "reveal": "always", "panel": "dedicated" },
            "problemMatcher": [],
            "hide": true
        },
        {
            "label": "UNO Q: Run LED Monitor (MPU)",
            "type": "process",
            "command": "ssh",
            "args": ["-t", "${config:unoq.boardHost}", "python3 -u led_monitor.py"],
            "dependsOn": ["UNO Q: Copy Python Script"],
            "presentation": { "reveal": "always", "panel": "dedicated", "focus": true },
            "problemMatcher": []
        },
        {
            "label": "UNO Q: Upload + Monitor",
            "dependsOrder": "sequence",
            "dependsOn": [
                "UNO Q: Upload Sketch (MCU)",
                "UNO Q: Run LED Monitor (MPU)"
            ],
            "problemMatcher": []
        }
    ]
}
```

| Task | What it does |
| --- | --- |
| UNO Q: Compile Sketch (MCU) | Default build task. Writes `led_bridge.ino.elf-zsk.bin` (Dynamic link mode) to `led_bridge/build/`. |
| UNO Q: Upload Sketch (MCU) | Compiles, creates `/tmp/remoteocd` on the board, copies the payload, then flashes it to `0x08100000` with the board's OpenOCD. |
| UNO Q: Run LED Monitor (MPU) | Copies `led_monitor.py` to the board's home folder and runs it over `ssh -t` in its own terminal panel. |
| UNO Q: Upload + Monitor | Runs the two tasks above in order. |

The three helper steps (staging folder, payload copy, script copy) are marked `"hide": true`, so they stay out of the task picker.

## 06Build, flash and monitor

#### 1 · Compile

Open any file in the workspace and press the build shortcut. Check the memory report at the end of the output.

Ctrl+Shift+B · macOS ⇧⌘B

#### 2 · Upload and monitor

Command Palette → **Tasks: Run Task** → **UNO Q: Upload + Monitor**. The LED starts blinking, then a new terminal panel opens with the script's output.

Ctrl+Shift+P · macOS ⇧⌘P

#### 3 · Stop

Click into the monitor's terminal panel and press Ctrl+C. The sketch keeps blinking. Run **UNO Q: Run LED Monitor (MPU)** to attach again.

### Alternative: run the script from the board's own terminal

If you prefer an interactive shell on the board, open a new terminal in VSCode, log in, and start the script yourself (copy it once with the task or with `scp`):

Terminalboard

```bash
ssh arduino@192.168.7.1          # or arduino@uno-q.local over Wi-Fi
python3 -u led_monitor.py
```

Same workflow without VSCode tasks

Run from the workspace folder in any terminal. Replace `arduino@192.168.7.1` with your board's address.

Terminalyour computer

```bash
# 1. Compile (from the workspace folder)
arduino-cli compile -b arduino:zephyr:unoq --output-dir led_bridge/build led_bridge

# 2. Copy the payload and flash the STM32 through the board's OpenOCD
ssh arduino@192.168.7.1 "mkdir -p /tmp/remoteocd"
scp led_bridge/build/led_bridge.ino.elf-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin
ssh arduino@192.168.7.1 "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"

# 3. Copy the Python script and run it in the board's terminal
scp mpu/led_monitor.py arduino@192.168.7.1:led_monitor.py
ssh -t arduino@192.168.7.1 "python3 -u led_monitor.py"
```

## 07Check the result

The LED on D13 blinks at 1 Hz (500 ms on, 500 ms off) and the monitor prints one line per change. The `+501 ms` column is measured on the MCU, so it shows the sketch's real period: the 500 ms delay plus the time spent sending the message. Example run:

UNO Q: Run LED Monitor (MPU)example output

```
Connected to arduino-router (/var/run/arduino-router.sock)
Registered 'led_state' - waiting for the MCU... (Ctrl+C to stop)
10:41:07.215  D13 ON   mcu t=48211 ms
10:41:07.717  D13 OFF  mcu t=48712 ms  (+501 ms)
10:41:08.218  D13 ON   mcu t=49213 ms  (+501 ms)
10:41:08.720  D13 OFF  mcu t=49714 ms  (+501 ms)
^C
Stopping.
```

The first line may show only `mcu t=…` without a period, because the script needs two messages to compute one. Wall-clock times on the left come from Linux and can jitter by a few milliseconds; the MCU column does not.

## 08Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Arduino_RouterBridge.h: No such file` | Library missing. Run `arduino-cli lib update-index`, then `arduino-cli lib install Arduino_RouterBridge`. |
| Task says `arduino-cli` (or `ssh`) not found | VSCode inherits `PATH` when it starts. Add the tool to `PATH`, then fully restart VSCode. |
| Every task step asks for a password | Key authentication is not set up. Run `ssh-copy-id` to the board (or append your public key to `~/.ssh/authorized_keys` on the board). |
| Upload succeeds but the LED never blinks | The loader on the STM32 is older than your core. Flash the loader `.bin` from the core's `firmwares` folder once, as in your setup guide, then upload again. Also make sure no App Lab app is running and reflashing the MCU. |
| `Could not register 'led_state': … route already exists` | Another process owns the name: an older copy of the script or an App Lab app. On the board run `pkill -f led_monitor.py`, or stop the App Lab app, then start the monitor again. |
| `Permission denied on /var/run/arduino-router.sock` | Your user cannot open the router's socket. Check with `ls -l /var/run/arduino-router.sock` and run the script with `sudo`, or add the user to the socket's group. |
| `arduino-router is not running` | Restart it with `sudo systemctl restart arduino-router`. Follow its log with `journalctl -u arduino-router -f`. |
| Script registers but prints nothing while the LED blinks | The names differ between `Bridge.notify()` and `METHOD` in the script. They are case-sensitive. |
| Monitor keeps running after closing the panel | Always stop with Ctrl+C. If a copy is left behind, the next start reports *route already exists*; see above. |

Built on the Dynamic link mode workflow (FQBN `arduino:zephyr:unoq`, payload `*.ino.elf-zsk.bin`, sketch address `0x08100000`). The Python script was tested against a simulated `arduino-router` socket; run the full workflow once on your board to confirm.