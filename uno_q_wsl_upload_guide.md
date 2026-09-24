# Arduino UNO Q: WSL & USBIPD Upload Guide

Because the Arduino UNO Q is a dual-core system (a Linux processor and a Zephyr microcontroller), the upload process is highly complex. When using Windows Subsystem for Linux (WSL) via `usbipd`, the standard `arduino-cli upload` command often freezes. 

This happens because the official upload scripts attempt to push a massive 2MB Zephyr OS file alongside your sketch. The virtual USB bridge (`usbipd`) chokes on this large bulk transfer and drops the connection.

Here is the complete, bulletproof "hacker" method to compile and upload a sketch entirely within WSL, bypassing the bugs by pushing only what is necessary.

---

## Part 1: Environment Setup (Windows / WSL)

Since WSL does not natively support USB devices, you must bridge the Arduino connection from Windows to Linux. You can do this using the command line (`usbipd-win`) or a graphical interface (**WSL USB Manager**).

### 1. Enable USB Passthrough (Windows)

#### Option A: Command Line (`usbipd-win`)
1. Open Windows PowerShell as Administrator and install `usbipd-win`:
   ```powershell
   winget install --interactive --exact dorssel.usbipd-win
   ```
2. Plug in the Arduino UNO Q via USB and list connected devices:
   ```powershell
   usbipd list
   ```
3. Locate the board's `BUSID` and attach it to WSL. By adding the `--auto-attach` flag, Windows will automatically reconnect the board to WSL every time you plug it in or reset it:
   ```powershell
   usbipd bind --busid <BUSID>
   usbipd attach --wsl --busid <BUSID> --auto-attach
   ```

#### Option B: Graphical Interface (WSL USB Manager)
If you prefer a visual interface instead of typing commands every time you plug in the board, you can use **WSL USB Manager**. It acts as a graphical frontend for `usbipd`.
1. Open Windows PowerShell and install the GUI tool:
   ```powershell
   winget install --interactive --exact alelec.WSLUSBManager
   ```
   *(Note: This automatically installs the core `usbipd-win` engine as a dependency if you don't already have it).*
2. Open **WSL USB Manager** from your Windows Start Menu.
3. Plug in the Arduino UNO Q. It will appear in the list of USB devices.
4. Click the **Attach** toggle next to the device to bind it and instantly connect it to your active WSL instance.
5. **(Recommended)** Click the **Auto Attach** toggle for this device. This ensures that whenever the board reboots or is plugged in, it will automatically route straight to WSL without you having to open the manager again.

### 2. Configure the Linux Environment (WSL)
Before you can compile, you must install the Arduino CLI, the Zephyr core, and the Android Debug Bridge (ADB) inside your WSL instance.

**Install System ADB**
Because the tools need to talk to the board's Linux processor over USB, you must install the system-wide ADB tool first.
```bash
sudo apt update && sudo apt -y install adb
```

**Install & Configure `arduino-cli`**
In your VSCode WSL terminal, install the CLI to a dedicated directory (e.g., `/opt/arduino-uno-q-tools/`) and install the Zephyr core:
```bash
# Create the directory (requires sudo to write to /opt/)
sudo mkdir -p /opt/arduino-uno-q-tools

# Install the CLI binary globally
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sudo BINDIR=/opt/arduino-uno-q-tools sh

# Add the custom directory to your system PATH so you can call it globally
echo 'export PATH="$PATH:/opt/arduino-uno-q-tools"' >> ~/.bashrc
source ~/.bashrc

# Create ecosystem directories and grant global write permissions (solves the staging error)
sudo mkdir -p /opt/arduino-uno-q-tools/{data,downloads,user}
sudo chmod -R a+w /opt/arduino-uno-q-tools/{data,downloads,user}

# Initialize config and force it to install libraries/cores into our custom /opt/ folder
arduino-cli config init
arduino-cli config set directories.data /opt/arduino-uno-q-tools/data
arduino-cli config set directories.downloads /opt/arduino-uno-q-tools/downloads
arduino-cli config set directories.user /opt/arduino-uno-q-tools/user

# Install the Zephyr core (files will be written to /opt/ by the normal user)
arduino-cli core update-index
arduino-cli core install arduino:zephyr
```

**Verify ADB Connection:**
Verify your board is successfully bridged by running `adb devices` in WSL. You should see an alphanumeric serial number (e.g., `1621644798 device`).

> [!TIP]
> **No Permissions / Missing Udev Rules Error?**
> If `adb devices` throws a `no permissions` error, it is because WSL doesn't always run the background `udev` permissions daemon properly. The easiest fix is to kill the ADB server and restart it as `root` so it can access the USB ports directly:
> ```bash
> sudo adb kill-server && sudo adb start-server
> adb devices
> ```

---

## Part 2: Compilation

When you compile a sketch for the UNO Q, `arduino-cli` does not produce a standalone executable. Instead, the `zephyr-sketch-tool` compiles your code into a raw payload (`.bin`) designed to be dynamically loaded by the board's existing Zephyr OS.

**1. Create the Sketch File**
Arduino sketches require the main `.ino` file to be inside a folder of the exact same name. Let's create a `led` folder and place your `led.ino` code inside it using the terminal:

```bash
mkdir -p led
cat << '__EOF__' > led/led.ino
#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50); // Simple software debounce
  digitalWrite(LED_BUILTIN, LOW);
  delay(50); // Simple software debounce
}
__EOF__
```

**2. Compile the Sketch:**
From the directory containing your new `led` folder, run:
```bash
arduino-cli compile -b arduino:zephyr:unoq --output-dir ./led/build ./led
```
* **Why `--output-dir`?** By default, `arduino-cli` hides compiled binaries deep in a hidden `/root/.cache/` folder. This flag forces it to save the output neatly into `./led/build/`, making it easy to grab the `led.ino.elf-zsk.bin` file.

---

## Part 3: The Manual Upload (Bypassing `usbipd` Bugs)

Because `arduino-cli upload` hangs on large files over `usbipd`, we manually extract the tiny sketch payload and flash it using the board's internal hardware debugger.

**Extract the Board Serial:**
Run this command to automatically parse your board's serial number into an environment variable so the subsequent commands can use it:
```bash
export ADB_SERIAL=$(adb devices | grep -w device | cut -f1)
```

**1. Create a Staging Folder on the Board**
Use ADB to create a temporary folder in the board's RAM.
```bash
adb -s ${ADB_SERIAL} shell mkdir -p /tmp/remoteocd
```

**2. Push the Sketch Payload**
Push ONLY your tiny compiled sketch binary to the board. Because the file is small (usually <100KB), `usbipd` will transfer it instantly without freezing.
```bash
adb -s ${ADB_SERIAL} push ./led/build/led.ino.elf-zsk.bin /tmp/remoteocd/sketch.bin
```

**3. Flash the Microcontroller**
Run the board's internal OpenOCD daemon to flash the STM32 microcontroller natively.
```bash
adb -s ${ADB_SERIAL} shell "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'shutdown'"
```

### Understanding the Flash Command
Because the Arduino UNO Q has two processors (a Linux processor and an STM32 microcontroller), this command runs *on the Linux processor* to directly program the STM32. It strings together OpenOCD (Open On-Chip Debugger) commands to manipulate the STM32's hardware at a very low level:

*   **`/opt/openocd/bin/openocd`**: The OpenOCD executable residing on the board's Linux filesystem.
*   **`-d2`**: Sets the debug output level to `2` (Info). This suppresses overly verbose debugging logs while still showing critical success/failure messages.
*   **`-s /opt/openocd ...`**: The `-s` flags add specific directories to OpenOCD's search path so it can locate necessary scripts and configuration files.
*   **`-f openocd_gpiod.cfg`**: Loads the board-specific hardware interface configuration. This tells OpenOCD how the Linux processor should use its GPIO pins to "bitbang" the SWD (Serial Wire Debug) protocol to communicate with the STM32 microcontroller.
*   **`-c 'init'`**: Initializes the OpenOCD daemon, connects to the STM32 microcontroller via the SWD interface, and prepares it for commands.
*   **`-c 'reset halt'`**: Issues a hard reset to the STM32 and immediately halts its CPU execution. This ensures the microcontroller is in a known, stable state before attempting to write to its flash memory.
*   **`-c 'flash write_image erase ... 0x8100000 bin'`**: The core flash command. It takes the binary file we just pushed to the board's RAM (`/tmp/remoteocd/sketch.bin`), automatically `erase`s the required destination sectors, and writes the sketch into **Bank 2** of the STM32's internal flash memory starting at the hexadecimal offset `0x08100000`. (Bank 1 contains the Zephyr OS).
*   **`-c 'reset'`**: Issues another hardware reset so the STM32 registers the newly written memory.
*   **`-c 'sleep 100'`**: Pauses for 100 milliseconds to give the STM32 enough time to finish its reset cycle.
*   **`-c 'mww 0x40036400 0xCAFFEEEE'`**: "Memory Write Word". This is the crucial **Magic Boot Trigger**. It writes the hex value `0xCAFFEEEE` directly into an RTC (Real-Time Clock) backup memory register at address `0x40036400`. When the Zephyr OS bootloader starts up, it reads this register; if it sees this magic word, it knows to jump execution to your user sketch in Bank 2 instead of running the default factory firmware!
*   **`-c 'shutdown'`**: Gracefully closes the OpenOCD daemon and exits the ADB shell, returning control to your WSL terminal.

---

## Part 4: Automating via VSCode Tasks

If you prefer to streamline the compilation and upload process using VSCode's graphical task runner, you can set up a workspace with a custom `tasks.json` configuration. 

Run the following block of commands in your WSL terminal to construct the workspace:

```bash
# 1. Create a workspace folder and its hidden .vscode configuration directory
mkdir -p arduino-uno-q-development/.vscode

# 2. Move the previously created 'led' sketch folder into the new workspace
mv led/ arduino-uno-q-development/

# 3. Enter the workspace directory
cd arduino-uno-q-development/

# 4. Generate the tasks.json file containing the dynamic build and upload definitions
cat << '__EOF__' > .vscode/tasks.json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch",
            "type": "shell",
            "command": "/opt/arduino-uno-q-tools/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\"",
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "reveal": "always",
                "panel": "new"
            },
            "problemMatcher": []
        },
        {
            "label": "Arduino UNO Q: Upload Current Sketch",
            "type": "shell",
            "command": "/opt/arduino-uno-q-tools/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\" && export ADB_SERIAL=$(adb devices | grep -w device | cut -f1) && adb -s ${ADB_SERIAL} shell mkdir -p /tmp/remoteocd && adb -s ${ADB_SERIAL} push \"${fileDirname}/build/\"*.elf-zsk.bin /tmp/remoteocd/sketch.bin && adb -s ${ADB_SERIAL} shell \"/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset run' -c 'sleep 500' -c 'halt' -c 'mww 0x40036400 0xCAFFEEEE' -c 'reset run' -c 'shutdown'\"",
            "group": "test",
            "presentation": {
                "reveal": "always",
                "panel": "new"
            },
            "problemMatcher": []
        }
    ]
}
__EOF__
```

### Explanation of the Commands:
* **`mkdir -p ...` and `mv ...`:** These commands structure your files into a proper VSCode workspace layout. 
* **`cat << '__EOF__' > ...`:** This creates the `tasks.json` file. 
* **`\"${fileDirname}\"`:** A dynamic VSCode variable. This ensures that the tasks compile and upload *whichever sketch folder you currently have actively open* in the editor!
* **`$(adb devices | ...)`:** The dynamic subshell command we built earlier. When VSCode fires the upload shell task, this automatically extracts your ADB serial number on the fly and injects it into the command.

### How to Launch the Tasks in VSCode
To use this new configuration:
1. Open VSCode and go to **File -> Open Folder...** and select the `arduino-uno-q-development` folder.
2. Open the `led/led.ino` file in the editor so it becomes the active file.
3. **To Compile:** Press `Ctrl + Shift + B`. This will automatically trigger the default build task (`Compile Current Sketch`).
4. **To Upload:** Open the Command Palette (`Ctrl + Shift + P`), type **Tasks: Run Task**, hit Enter, and select `Arduino UNO Q: Upload Current Sketch`.

