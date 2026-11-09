# Arduino UNO Q: WSL & USBIPD Upload Guide

Because the Arduino UNO Q is a dual-core system (a Linux processor and a Zephyr microcontroller), the upload process works differently than on a classic Arduino. The official upload tool (`remoteocd`) transfers two files over ADB: your compiled sketch payload and the Zephyr loader image `zephyr-arduino_uno_q_stm32u585xx.elf` (~2.2 MB, most of which is debug information). The board-side OpenOCD script then verifies both against the microcontroller's flash and rewrites each only if it differs.

Alternatively, you can push only the sketch payload and flash it directly using the board's on-board OpenOCD—giving you full, transparent control over the flashing and debugging workflow from WSL (over ADB or SSH).

Here is the complete method to set up your WSL environment, configure wired/wireless IP networking and headless system optimization, and then compile, upload, and debug sketches entirely within WSL.

---

## Board Architecture & Pinout Mapping

The Arduino UNO Q has a dual-chip architecture with a clear division of responsibilities:
* **Microcontroller (MCU — STM32U585):** Handles all real-time hardware I/O. **All standard Arduino Uno R3 header pins (D0–D13, A0–A5, SPI, I2C, Power/Reset) are wired directly to the STM32.**
* **Microprocessor (MPU — Qualcomm Dragonwing QRB2210):** Runs Debian Linux. It manages high-speed computing, networking, multimedia, system storage, status LEDs, and internal flashing/debugging lines.

### 1. Microcontroller (STM32U585) — Arduino Header Pins

The physical female headers on the edges of the board are routed to the STM32U585:

| Arduino Pin | STM32 Pin | Primary / Alternate Functions (Zephyr DTS) |
|---|---|---|
| **D0 (RX)** | `PB7` | Digital I/O, UART RX |
| **D1 (TX)** | `PB6` | Digital I/O, UART TX |
| **D2** | `PB3` | Digital I/O, Timer 2 PWM (`TIM2_CH2`) |
| **D3 (~PWM)** | `PB0` | Digital I/O, Timer 3 PWM (`TIM3_CH3`) |
| **D4** | `PA12` | Digital I/O |
| **D5 (~PWM)** | `PA11` | Digital I/O, Timer 1 PWM (`TIM1_CH4`) |
| **D6 (~PWM)** | `PB1` | Digital I/O, Timer 3 PWM (`TIM3_CH4`) |
| **D7** | `PB2` | Digital I/O, Timer 8 PWM (`TIM8_CH4N`) |
| **D8** | `PB4` | Digital I/O, Timer 3 PWM (`TIM3_CH1`) |
| **D9 (~PWM)** | `PB8` | Digital I/O, Timer 4 PWM (`TIM4_CH3`) |
| **D10 (~PWM)** | `PB9` | Digital I/O, Timer 4 PWM (`TIM4_CH4`), SPI Chip Select (`SS`) |
| **D11 (~PWM)** | `PB15` | Digital I/O, Timer 1 PWM (`TIM1_CH3N`), SPI `MOSI` |
| **D12** | `PB14` | Digital I/O, Timer 1 PWM (`TIM1_CH2N`), SPI `MISO` |
| **D13** | `PB13` | Digital I/O, Timer 1 PWM (`TIM1_CH1N`), SPI `SCK`, Built-in LED |
| **A0** | `PA4` | Analog Input (`ADC1_IN9`) |
| **A1** | `PA5` | Analog Input (`ADC1_IN10`) |
| **A2** | `PA6` | Analog Input (`ADC1_IN11`) |
| **A3** | `PA7` | Analog Input (`ADC1_IN12`) |
| **A4 (SDA)** | `PC1` | Analog Input (`ADC1_IN2`), `I2C3_SDA` |
| **A5 (SCL)** | `PC0` | Analog Input (`ADC1_IN1`), `I2C3_SCL` |
| **RESET** | `NRST` | Microcontroller Hardware Reset |

*(The STM32 also controls onboard RGB LED channels `LED3` and `LED4`).*

### 2. Microprocessor (Dragonwing QRB2210) — Interfaces & Peripherals

The Dragonwing Linux MPU does **not** expose general-purpose pins to the Arduino headers. Instead, it manages the board-level multimedia, communications, and power:

| Interface / Peripheral | Dragonwing Assignment | Description |
|---|---|---|
| **USB-C Port** | `DWC3 USB Controller` | Power delivery, ADB access, Linux serial console (`ttyGS0`), and USB host/device |
| **Wireless** | `Qualcomm WCN3950/WCN3988` | Dual-band Wi-Fi (802.11ac) & Bluetooth 5.0 (`hci0`) |
| **Camera Interface** | `MIPI CSI (CamSS) + I2C-0` | FPC connector for camera modules |
| **Display / Video** | `MIPI DSI + ANX7625 Bridge` | Display / DP / HDMI output |
| **Audio** | `SoundWire + PM4125 Codec` | Analog audio output and microphone jack |
| **Storage** | `SDHCI / MMC0` | Onboard high-speed eMMC 5.1 storage |
| **Hardware Buttons** | `GPIO 96` (Vol Up), `GPIO 36` (Vol Down) | Fastboot, EDL recovery, and system keys |
| **Status LEDs** | `GPIO 39` (Panic Red), `GPIO 40` (WLAN Green), `GPIO 47` (BT Blue), `GPIO 41/42/60` (User RGB) | Linux system and network indicator LEDs |

![Arduino UNO Q Pinout Diagram](C:/Users/nxky0/.gemini/antigravity/brain/d80e0346-a00c-4d23-b268-4ba7e521cb63/uno_q_pinout.png)

### 3. Internal Inter-Processor Interconnect (Dragonwing ↔ STM32)

The two chips communicate across internal PCB traces:

1. **High-Speed Serial Bridge (RPC Data):**
   * **Dragonwing Side:** `/dev/ttyHS1` (Qualcomm GENI High-Speed UART)
   * **STM32 Side:** `USART3` (`PB10` TX, `PB11` RX)
   * **Handshake Line:** Dragonwing `GPIO 70`
   * *Managed by the `arduino-router` daemon at 115200 baud for `Arduino_RouterBridge` RPC communication.*

2. **SWD Hardware Flash & Debug Bus:**
   * **Dragonwing `GPIO 25`** ➔ STM32 `SWDIO`
   * **Dragonwing `GPIO 26`** ➔ STM32 `SWCLK`
   * **Dragonwing `GPIO 38`** ➔ STM32 `NRST` (Hardware Reset)
   * *Used by the Linux `openocd` daemon to flash sketches and provide the GDB debug server.*

### Summary Rule of Thumb
* For reading sensors, toggling relays, PWM-controlling motors, or using standard Arduino shields: **Program the STM32 via Arduino C++.**
* For machine learning, computer vision, web networking, or running Python scripts: **Use the Dragonwing Linux processor and communicate with the STM32 via `Bridge` (RPC).**

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
   winget install -e --id nickbeth.wsl-usb-manager
   ```
   *(Note: This automatically installs or interfaces with the core `usbipd-win` engine as a dependency).*
2. Open **WSL USB Manager** from your Windows Start Menu.
3. Plug in the Arduino UNO Q. It will appear in the list of USB devices.
4. Click the **Attach** toggle next to the device to bind it and instantly connect it to your active WSL instance.
5. **(Recommended)** Click the **Auto Attach** toggle for this device. This ensures that whenever the board reboots or is plugged in, it will automatically route straight to WSL without you having to open the manager again.

### 2. Configure the Linux Environment (WSL)
Before you can flash the board or compile sketches, you must install the system prerequisites, the OS image flasher (`arduino-flasher-cli`), and the Arduino CLI (`arduino-cli`) with the Zephyr core inside your WSL instance.

**Install System Packages (`adb`, `curl`, `gdb-multiarch`, `zstd`)**
Because the tools need to talk to the board's Linux processor over USB, you must install the system-wide ADB tool first. `curl` is required to download the Arduino tools in the next steps, `gdb-multiarch` is needed for real-time hardware debugging in VSCode (Appendix 4), and `zstd` is used to extract and pack OS image archives (Part 4).
```bash
sudo apt update && sudo apt -y install adb curl gdb-multiarch zstd
```

#### A. Flashing / Restoring the Debian Linux OS Image (`arduino-flasher-cli`)
If you need to update, flash, or recover the full Debian Linux OS onto the Qualcomm Dragonwing processor's eMMC from Ubuntu WSL, use **`arduino-flasher-cli`**:

##### Step 1: Install `arduino-flasher-cli` and Download a Specific OS Version
1. Create the `/opt/arduino-uno-q-tools/flasher` directory, download the latest Linux x86_64 release of [`arduino-flasher-cli`](https://github.com/arduino/arduino-flasher-cli/releases), and add it to your system `PATH`:
   ```bash
   sudo mkdir -p /opt/arduino-uno-q-tools/flasher
   sudo chmod -R a+w /opt/arduino-uno-q-tools/flasher
   cd /opt/arduino-uno-q-tools/flasher

   FLASHER_URL=$(curl -fsSL https://api.github.com/repos/arduino/arduino-flasher-cli/releases/latest | grep -oE "https://[a-zA-Z0-9./_-]+linux-amd64\.tar\.gz")
   curl -fsSL "${FLASHER_URL}" | tar -xz

   # Add flasher directory to PATH and create symlink in /usr/local/bin (so sudo also finds it)
   echo 'export PATH="$PATH:/opt/arduino-uno-q-tools/flasher"' >> ~/.bashrc
   source ~/.bashrc
   sudo ln -sf /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli /usr/local/bin/arduino-flasher-cli
   ```
2. **Disable and permanently block the conflicting Linux `qcserial` driver:**
   By default, the WSL Linux kernel automatically loads `qcserial` (`Qualcomm USB modem converter`) every time `05c6:9008` enumerates, hijacking the USB bulk endpoints onto `/dev/ttyUSB0`. Because `udev` can still reload `qcserial` by modalias even when blacklisted, add both `blacklist` and `install qcserial /bin/false` *(see [Appendix 2: The `qcserial` Driver](#appendix-2-the-qcserial-driver--impact-on-normal-operation) for details on why this is necessary and why it does not affect normal operation)*:
   ```bash
   printf "blacklist qcserial\ninstall qcserial /bin/false\n" | sudo tee /etc/modprobe.d/blacklist-qcserial.conf
   sudo modprobe -r qcserial usb_wwan usbserial 2>/dev/null || sudo modprobe -r qcserial 2>/dev/null || true
   ```
3. **Patch the embedded `qdl` binary inside `arduino-flasher-cli` for WSL2 (`ZlpAwareHost="0"`):**
   Due to a known `usbipd-win` driver bug ([`dorssel/usbipd-win#924`](https://github.com/dorssel/usbipd-win/issues/924)), 0-byte USB bulk Zero-Length Packets (ZLPs) are dropped when forwarded from WSL2 to Windows, causing `xbl_a` to freeze at `33%` and time out after 30 seconds. Run the following `sed` command to patch the embedded `qdl` binary (saving a backup at `arduino-flasher-cli.bak`) so it configures the board's Firehose programmer with `ZlpAwareHost="0"` and skips 0-byte ZLP transfers *(see [Appendix 1: The `ZlpAwareHost="0"` Fix](#appendix-1-the-zlpawarehost0-fix--compatibility-with-future-qdl-releases) for full details)*:
   ```bash
   sudo LC_ALL=C sed -i.bak \
     -e 's/ZLPAwareHost/ZlpAwareHost/g' \
     -e 's/\x48\x89\xdf\xb9\x01\x00\x00\x00\(\xba....\xbe....\x31\xc0\xe8\)/\x48\x89\xdf\xb9\x00\x00\x00\x00\1/g' \
     -e 's/\x48\x85\xd2\x74\x18\x48\x83\xc4\x28\x89\xe8/\x48\x85\xd2\x90\x90\x48\x83\xc4\x28\x89\xe8/g' \
     /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli
   ```
4. List the available Debian OS image versions for the UNO Q:
   ```bash
   arduino-flasher-cli list
   ```
   *(Example versions: `20260528-558`, `20260407-523`, `20251229-457`, etc.)*
5. Download your desired specific version into `/opt/arduino-uno-q-tools/flasher`:
   ```bash
   arduino-flasher-cli download unoq --version <VERSION> --dest-dir /opt/arduino-uno-q-tools/flasher
   # Example:
   # arduino-flasher-cli download unoq --version 20260528-558 --dest-dir /opt/arduino-uno-q-tools/flasher
   ```
   This saves the archive as `/opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-<VERSION>.tar.zst`.

##### Step 2: Put the Board into EDL (Emergency Download) Mode
1. **Unplug** the USB-C cable from the Arduino UNO Q.
2. **Short the EDL pins** on the `JCTL` 2x5 header by connecting **Pin 9 (`USB_BOOT` / `GPIO_95`)** to **Pin 10 (`GND`)** (the bottom row of the 2x5 header) using a jumper wire or tweezers.
3. **Plug the USB-C cable back in** while keeping the jumper connected. The board will enumerate as a Qualcomm EDL device (`05c6:9008`).

![Arduino UNO Q EDL Jumper Location](C:/Users/nxky0/.gemini/antigravity/brain/d80e0346-a00c-4d23-b268-4ba7e521cb63/uno_q_edl_jumper.png)

##### Step 3: Pass the EDL USB Device to WSL (With Auto-Attach)
Because the USB hardware ID changes in EDL mode (`05c6:9008` Qualcomm HS-USB QDLoader 9008), pass the new device to WSL using either method (make sure **Auto-Attach** is enabled):

* **Using WSL USB Manager (GUI):**
  Open **WSL USB Manager**, locate the newly enumerated Qualcomm device (e.g., `QUSB_BULK` / `Qualcomm HS-USB QDLoader 9008` / `05c6:9008`), and enable both **Attach** and **Auto Attach**.

* **Using PowerShell CLI (`usbipd`):**
  From an elevated Windows PowerShell:
  ```powershell
  usbipd list
  usbipd bind --busid <EDL_BUSID>
  usbipd attach --wsl --busid <EDL_BUSID> --auto-attach
  ```

##### Step 4: Flash the Downloaded Image into the Board
1. Start the flash command with `sudo` (passing `--root-size 20GB` skips the pre-flash GPT check so `qdl` connects in a single pass without resetting the USB bus mid-run):
   ```bash
   sudo arduino-flasher-cli flash unoq /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-<VERSION>.tar.zst --root-size 20GB
   # Example:
   # sudo arduino-flasher-cli flash unoq /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558.tar.zst --root-size 20GB
   ```
2. Once complete, unplug the USB cable, **remove the EDL jumper wire**, and plug the board back in to boot normally.

> [!WARNING]
> **Troubleshooting `waiting for programmer... / flashing of xbl_a failed`:**
> 1. **Always power-cycle after a failed attempt:** Once an EDL Sahara handshake fails, the Qualcomm boot ROM waits in Firehose/Sahara state until power is removed. Unplug the USB-C cable and plug it back in before retrying.
> 2. **Verify `qcserial` is not loaded:** Run `lsmod | grep qcserial` in WSL. Because `arduino-flasher-cli` resets the board after checking the GPT partition size (`read.xml`), **Auto-Attach** in `usbipd` / WSL USB Manager must be enabled and `/etc/modprobe.d/blacklist-qcserial.conf` must include `install qcserial /bin/false` so `udev` does not reload `qcserial` when the board re-attaches.
> 3. **Ensure the `ZlpAwareHost="0"` patch is applied:** Without the Step 1.3 patch, `qdl` sends `ZLPAwareHost="1"` (with uppercase `LP`, which the board's Firehose parser ignores due to case sensitivity) and relies on 0-byte USB ZLPs that `usbipd-win` drops, causing `xbl_a` to freeze at `33%`.
3. **Verify ADB Connection:**
   Verify your board is successfully bridged by running `adb devices` in WSL. You should see an alphanumeric serial number (e.g., `1621644798 device`).

   > [!TIP]
   > **No Permissions / Missing Udev Rules Error?**
   > If `adb devices` throws a `no permissions` error, it is because WSL doesn't always run the background `udev` permissions daemon properly. The easiest fix is to kill the ADB server and restart it as `root` so it can access the USB ports directly:
   > ```bash
   > sudo adb kill-server && sudo adb start-server
   > adb devices
   > ```

#### B. Install & Configure `arduino-cli`
In your VSCode WSL terminal, install the CLI to a dedicated directory (e.g., `/opt/arduino-uno-q-tools/`) and install the Zephyr core:
```bash
# Create the directory (requires sudo to write to /opt/)
sudo mkdir -p /opt/arduino-uno-q-tools

# Install the CLI binary globally
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sudo BINDIR=/opt/arduino-uno-q-tools sh

# Add the custom directory to your system PATH and set the default config file location
echo 'export PATH="$PATH:/opt/arduino-uno-q-tools"' >> ~/.bashrc
echo 'export ARDUINO_CONFIG_FILE="/opt/arduino-uno-q-tools/arduino-cli.yaml"' >> ~/.bashrc
source ~/.bashrc

# Create ecosystem directories and grant write permissions across the folder
sudo mkdir -p /opt/arduino-uno-q-tools/{data,downloads,user}
sudo chmod -R a+w /opt/arduino-uno-q-tools

# Initialize config directly into /opt/arduino-uno-q-tools/ and configure directory paths
arduino-cli config init --dest-dir /opt/arduino-uno-q-tools --overwrite
arduino-cli config set directories.data /opt/arduino-uno-q-tools/data
arduino-cli config set directories.downloads /opt/arduino-uno-q-tools/downloads
arduino-cli config set directories.user /opt/arduino-uno-q-tools/user

# Refresh BOTH indexes: the zephyr core depends on libraries (e.g. Arduino_RouterBridge)
# that are resolved from library_index.json, which `core update-index` does not refresh
arduino-cli core update-index
arduino-cli lib update-index

# Install the Zephyr core (files will be written to /opt/ by the normal user)
arduino-cli core install arduino:zephyr
```

---

## Part 2: Direct Wired IP Networking (Ethernet-over-USB via CDC-NCM)

Instead of relying on virtual USB bridging (`usbipd-win`) and ADB for day-to-day development, you can configure the UNO Q's USB-C port to act as an **Ethernet-over-USB gadget**.

This gives you the ultimate development experience: **a single USB-C cable supplies 5V power AND a direct high-speed TCP/IP network link.**
* Windows natively recognizes the board as a USB Network Adapter without custom drivers.
* WSL accesses the board directly over local IP (`192.168.7.1` or `uno-q.local`) without needing `usbipd-win` passthrough.
* You can compile, flash, debug, and log in via standard `ssh` and `scp` with zero wireless latency, even with no Wi-Fi router.

The Dragonwing Linux kernel already includes the compiled kernel modules: `usb_f_ncm.ko` (modern CDC-NCM standard), `usb_f_rndis.ko`, and `usb_f_ecm.ko`.

### Step 1: Initialize User Password & Add the NCM Function to the USB Gadget Script

On a freshly flashed Debian image, the `arduino` account password is marked as expired (`password must be changed` in `/etc/shadow`), and `/etc/sudoers.d/90-debos` references `debian` instead of `arduino`. First, initialize the `arduino` user's password via the board's built-in `NOPASSWD` helper (`/usr/local/bin/arduino-passwd`) and update `90-debos` so non-interactive `sudo` commands over ADB and SSH do not fail:

```bash
# 1. Set a password for the arduino user (clears the initial expired-password lock)
adb shell "echo 'YOUR_CHOSEN_PASSWORD' | sudo /usr/local/bin/arduino-passwd"

# 2. Update /etc/sudoers.d/90-debos from 'debian' to 'arduino' for non-interactive sudo
adb shell "echo 'YOUR_CHOSEN_PASSWORD' | sudo -S sed -i 's/^debian /arduino /' /etc/sudoers.d/90-debos"
```

Next, patch `/usr/lib/android-sdk/platform-tools/adbd-usb-gadget` (which manages the USB-C port) to enable the CDC-NCM USB Ethernet gadget in `setup()` and `reset()` (saving a backup at `adbd-usb-gadget.bak`):

```bash
adb shell 'grep -q "ncm.usb0" /usr/lib/android-sdk/platform-tools/adbd-usb-gadget || sudo sed -i.bak \
  -e "s|ln -s functions/acm.GS0 configs/c.1|&\n\n    # create the USB Ethernet (NCM) function\n    modprobe usb_f_ncm\n    mkdir -p functions/ncm.usb0\n    ln -s functions/ncm.usb0 configs/c.1|" \
  -e "s|rm \${CONFIGFS_DIR}/configs/c.1/acm.GS0|&\n    rm -f \${CONFIGFS_DIR}/configs/c.1/ncm.usb0|" \
  /usr/lib/android-sdk/platform-tools/adbd-usb-gadget'
```

* **What this command does:**
  1. Checks if `ncm.usb0` is already present (`grep -q "ncm.usb0"`) so running the command more than once will not duplicate lines.
  2. In `setup()`, appends `modprobe usb_f_ncm`, `mkdir -p functions/ncm.usb0`, and `ln -s functions/ncm.usb0 configs/c.1` directly below `ln -s functions/acm.GS0 configs/c.1`.
  3. In `reset()`, appends `rm -f ${CONFIGFS_DIR}/configs/c.1/ncm.usb0` directly below `rm ${CONFIGFS_DIR}/configs/c.1/acm.GS0`.

### Step 2: Configure NetworkManager for `usb0` (Static IP & DHCP)

By default, `/usr/lib/udev/rules.d/85-nm-unmanaged.rules` sets `ENV{DEVTYPE}=="gadget", ENV{NM_UNMANAGED}="1"`, which causes NetworkManager to ignore `usb0` (`unmanaged`). Allow NetworkManager to manage `usb0` and configure it to assign the board IP `192.168.7.1` while serving DHCP (`192.168.7.x`) to your PC:

```bash
# 1. Allow NetworkManager to manage the USB gadget interface (usb0)
adb shell 'sudo sed -i.bak "/DEVTYPE/s/1/0/" /usr/lib/udev/rules.d/85-nm-unmanaged.rules'

# 2. Create the NetworkManager connection profile for usb0
adb shell "sudo tee /etc/NetworkManager/system-connections/usb0.nmconnection << 'EOF'
[connection]
id=usb0
type=ethernet
interface-name=usb0

[ipv4]
address1=192.168.7.1/24
method=shared

[ipv6]
method=ignore
EOF"

adb shell sudo chmod 600 /etc/NetworkManager/system-connections/usb0.nmconnection

# 3. Prevent usb0 DHCP from advertising itself as your PC's default internet gateway or DNS server
adb shell "sudo mkdir -p /etc/NetworkManager/dnsmasq-shared.d && printf 'dhcp-option=3\ndhcp-option=6\n' | sudo tee /etc/NetworkManager/dnsmasq-shared.d/no-default-route.conf"

adb shell sudo nmcli connection reload
```

### Step 3: Generate SSH Host Keys, Enable SSH, and Reboot

Because the factory Debian image ships without pre-generated `/etc/ssh/ssh_host_*` keys, generate the SSH host keys first, then enable `ssh` and mDNS (`avahi-daemon`) and reboot:

```bash
# 1. Generate missing SSH host keys (required before ssh.service can start)
adb shell sudo ssh-keygen -A

# 2. Enable SSH and mDNS (uno-q.local) discovery
adb shell sudo systemctl enable --now ssh
adb shell sudo systemctl enable --now avahi-daemon

# 3. Reboot the board to activate the USB Ethernet gadget
adb shell sudo reboot
```

> [!IMPORTANT]
> **Detach from `usbipd-win` / WSL USB Manager After Rebooting:**
> For Windows to mount the board's USB-NCM network adapter and share its `192.168.7.1` IP route with WSL, make sure the board is **Detached** in **WSL USB Manager** (or run `usbipd detach --busid <BUSID>` in PowerShell). Once detached from raw USB passthrough, both Windows and WSL can reach the board directly via `ssh arduino@192.168.7.1` (or `ssh arduino@uno-q.local`).

---

## Part 3: Headless Optimization (Disabling the Graphical Desktop)

By default, the Dragonwing Debian OS boots into `graphical.target` and runs **LightDM**, **Xorg**, and a **GTK Greeter** in the background, consuming **~250 MB to 300 MB of RAM** even when no physical screen is attached.

Switching to a clean, headless text-only server mode (`multi-user.target`) frees up substantial system memory and reduces heat/idle CPU load.

### 1. Disable the GUI and Switch to Headless Mode

Open an interactive shell on the board via SSH:

```bash
ssh arduino@192.168.7.1
# (or ssh arduino@uno-q.local if connected over Wi-Fi)
```

Then run:

```bash
# 1. Set the default boot target to headless (text-only / multi-user)
sudo systemctl set-default multi-user.target

# 2. Disable LightDM so the display manager does not start on boot
sudo systemctl disable lightdm

# 3. Stop LightDM and Xorg immediately (frees RAM without requiring a reboot)
sudo systemctl stop lightdm
```

### 2. What Changes?

* **RAM Freed:** ~250–300 MB of RAM is instantly reclaimed (no Xorg display server, GTK greeter, or desktop audio session daemons).
* **CPU & Power:** Idle CPU load drops and the board runs noticeably cooler.
* **What keeps working:** Everything essential—**SSH**, **Wi-Fi / NetworkManager**, **Docker**, **Python**, **ADB**, and the **Arduino Router** RPC bridge—runs under `multi-user.target` and remains fully functional.

### 3. Restoring the GUI (Rollback)

If you ever plug in a display shield or HDMI/DSI monitor and need the graphical desktop back, run over SSH:

```bash
sudo systemctl set-default graphical.target
sudo systemctl enable --now lightdm
```

---

## Part 4: Cloning & Backup (Creating a "Configured Image" & Restoring to Other Boards)

Once you have customized an Arduino UNO Q (installed `apt` packages, built custom Python virtual environments, enabled systemd daemons, or applied headless optimizations), you can capture this exact system state as a **configured image** and deploy it to other Arduino UNO Q boards. 

Because Qualcomm's Firehose architecture divides the internal eMMC storage into independent partitions, all customizations live on **`rootfs`** (`disk-sdcard.img.root`, ~7.55 GB) and **`userdata`** (`disk-sdcard.img.home`, ~742 MB). By streaming these two partitions over the working Ethernet-over-USB interface (`192.168.7.1`) into a configured directory alongside the original archive (`/opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured/`), you can use `arduino-flasher-cli` to mass-provision other boards in a single command.

```
+------------------------------------+
| Configured Master Board (Running)  |
|  - IP: 192.168.7.1 (Ethernet/USB)  |
|  - Dump /dev/disk/by-partlabel/*   |
+-----------------+------------------+
                  | SSH Stream (gzip/zstd)
                  v
+--------------------------------------------------------------------------+
| WSL Workstation                                                          |
|  /opt/arduino-uno-q-tools/flasher/                                       |
|   └── arduino-unoq-debian-image-20260528-558-configured/                |
|       ├── disk-sdcard.img.root (Configured 7.55 GB ext4)                 |
|       └── disk-sdcard.img.home (Configured 742 MB ext4)                  |
+-----------------+--------------------------------------------------------+
                  | arduino-flasher-cli (EDL Mode)
                  v
+------------------------------------+
| Target Arduino UNO Q Boards        |
|  - Exact cloned Debian OS & setup  |
+------------------------------------+
```

### Step 1: Sanitize Unique Identifiers on the Master Board

Log into the configured master board over the Ethernet-over-USB connection (`192.168.7.1`) to purge machine-specific IDs, caches, and temporary files so that restored boards do not encounter identity or network collisions:

```bash
ssh arduino@192.168.7.1
```

Run the sanitization sequence on the board:
```bash
# 1. Reset machine-id so systemd generates a fresh, unique ID on each cloned board's first boot
sudo truncate -s 0 /etc/machine-id
sudo rm -f /var/lib/dbus/machine-id

# 2. Regenerate fresh SSH host keys so ssh.service starts cleanly on cloned boards
sudo rm -f /etc/ssh/ssh_host_*_key*
sudo ssh-keygen -A

# 3. Clean apt package caches and system journals to minimize backup size
sudo apt-get clean
sudo journalctl --vacuum-time=1d

# 4. Flush dirty filesystem buffers to eMMC
sudo sync

# 5. Clear bash history and exit session
rm .bash_history && history -c && history -w && exit
```

> [!TIP]
> Any configured Wi-Fi credentials in `/etc/NetworkManager/system-connections/` will remain intact in the image. Cloned boards will immediately connect to your local Wi-Fi network upon booting without requiring any manual reconfiguration.

---

### Step 2: Prepare the Configured Image Template in WSL

From your WSL terminal, extract the base Qualcomm firmware, bootloaders, and XML manifests from the official image archive directly into a new folder named `arduino-unoq-debian-image-20260528-558-configured` (located alongside the original archive under `/opt/arduino-uno-q-tools/flasher/`):

```bash
# 1. Create the configured image directory
sudo mkdir -p /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured

# 2. Extract the base image structure directly into the configured folder
sudo tar --zstd -xf /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558.tar.zst -C /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured --strip-components=1
```

---

### Step 3: Stream & Shrink the Configured Partitions Over Ethernet-over-USB

Because `debos-grow-rootfs.service` expands `rootfs` (to 20 GB) and `userdata` (to ~10.2 GB) on first boot, use `/usr/sbin/e2image -raf` to stream only the **allocated (`ext4` used) blocks** over `192.168.7.1`, then shrink both images to their minimum size with `resize2fs -M` and `truncate` so they fit inside the partition layout of `rawprogram0.xml` (and automatically re-expand on any target board's first boot):

```bash
MASTER_IP="192.168.7.1"
IMG_DIR="/opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured"
```

#### 1. Stream & Shrink the Root Filesystem (`rootfs`)
```bash
# Stream only used ext4 blocks from rootfs over SSH
ssh arduino@${MASTER_IP} "sudo /usr/sbin/e2image -raf /dev/disk/by-partlabel/rootfs - | gzip -c" | gzip -dc | sudo dd of="${IMG_DIR}/disk-sdcard.img.root" bs=4M status=progress

# Shrink the ext4 filesystem and truncate the image file to match
sudo /usr/sbin/e2fsck -fy "${IMG_DIR}/disk-sdcard.img.root"
sudo /usr/sbin/resize2fs -M "${IMG_DIR}/disk-sdcard.img.root"
ROOT_BLOCKS=$(sudo /usr/sbin/tune2fs -l "${IMG_DIR}/disk-sdcard.img.root" | awk '/Block count:/ {print $3}')
sudo truncate -s $((ROOT_BLOCKS * 4096)) "${IMG_DIR}/disk-sdcard.img.root"
```

#### 2. Stream & Shrink the User Data Filesystem (`userdata` / `home`)
```bash
# Stream only used ext4 blocks from userdata over SSH
ssh arduino@${MASTER_IP} "sudo /usr/sbin/e2image -raf /dev/disk/by-partlabel/userdata - | gzip -c" | gzip -dc | sudo dd of="${IMG_DIR}/disk-sdcard.img.home" bs=4M status=progress

# Shrink the ext4 filesystem and truncate the image file to match
sudo /usr/sbin/e2fsck -fy "${IMG_DIR}/disk-sdcard.img.home"
sudo /usr/sbin/resize2fs -M "${IMG_DIR}/disk-sdcard.img.home"
HOME_BLOCKS=$(sudo /usr/sbin/tune2fs -l "${IMG_DIR}/disk-sdcard.img.home" | awk '/Block count:/ {print $3}')
sudo truncate -s $((HOME_BLOCKS * 4096)) "${IMG_DIR}/disk-sdcard.img.home"
```

Once completed, `${IMG_DIR}` represents a complete, deployment-ready distribution containing your exact Debian setup, user files, and daemons.

---

### Step 4: Pack into a Shareable `.tar.zst` Archive

Compress the configured image directory into a `.tar.zst` archive (matching the exact format of Arduino's official image releases) so it can be flashed or distributed:

```bash
cd /opt/arduino-uno-q-tools/flasher
sudo tar --zstd -cf /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured.tar.zst arduino-unoq-debian-image-20260528-558-configured/
sudo rm -rf arduino-unoq-debian-image-20260528-558-configured
```

---

### Step 5: Restore / Flash to Other Arduino UNO Q Boards

To provision any target Arduino UNO Q board with your configured image:

#### 1. Hardware EDL Mode
1. Unplug the target board from USB power.
2. Short **Pin 9 (`USB_BOOT`)** to **Pin 10 (`GND`)** on the `JCTL` 2x5 header with a jumper wire or tweezers.
3. Plug the USB-C cable in (the board enumerates as `05c6:9008` Qualcomm EDL).

#### 2. Attach to WSL
Using **WSL USB Manager** (or PowerShell `usbipd attach --wsl --busid <BUSID> --auto-attach`), verify the Qualcomm EDL device is attached to WSL.

#### 3. Execute the Flash Command
From WSL, run `arduino-flasher-cli` pointing to the packaged configured archive:

```bash
sudo /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli flash unoq \
  /opt/arduino-uno-q-tools/flasher/arduino-unoq-debian-image-20260528-558-configured.tar.zst \
  --root-size 20GB
```

#### 4. Reboot
1. Unplug the USB-C cable.
2. **Remove the EDL jumper wire from pins 9 and 10.**
3. Plug the USB-C cable back in.

The new board will boot up immediately into your fully configured Debian environment with all installed libraries, network configurations, background daemons, and system optimizations fully active.

---

## Part 5: Compilation

### 1. Creating Workspace Folder

First, create the `/workspace/arduino-uno-q/` directory with global read/write permissions (matching `/opt/arduino-uno-q-tools/`) and navigate into it:

```bash
sudo mkdir -p /workspace/arduino-uno-q
sudo chmod -R a+w /workspace/arduino-uno-q
cd /workspace/arduino-uno-q
```

### 2. Create the Sketch File

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

### 3. Understanding Link Modes

When you compile a sketch for the UNO Q, `arduino-cli` does not produce a standalone firmware. The microcontroller always runs a pre-built **Zephyr loader** (the "bootloader" firmware shipped with the core). Your sketch is built as an add-on that this loader starts. *How* the sketch is joined to the loader is controlled by the board's **Link mode** option.

*(For the complete technical breakdown of symbol resolution, LLEXT heap mechanics, and architectural trade-offs, see [Appendix 3: Link Modes (Dynamic vs. Static Architecture & Mechanics)](#appendix-3-link-modes-dynamic-vs-static-architecture--mechanics)).*

#### Side-by-side

| | Dynamic (default) | Static |
|---|---|---|
| FQBN | `arduino:zephyr:unoq` | `arduino:zephyr:unoq:link_mode=static` |
| Final link | Relocatable (`ld -r`) | Fully linked at fixed addresses |
| Symbol file used | `syms-dynamic.ld` (exported API only) | `syms-static.ld` (all loader symbols) |
| File to upload | `<sketch>.ino.elf-zsk.bin` | `<sketch>.ino.bin-zsk.bin` |
| `zephyr-sketch-tool` flag | *(none)* | `-prelinked` |
| Work done at boot | Load into RAM, resolve symbols by name, relocate | None, starts the pre-linked image |
| Must the loader match exactly? | No, only its exported API | **Yes**, byte-for-byte |
| Hardware Breakpoints | ❌ Fails (dynamic RAM address) | ✅ **Works!** (Fixed Flash address) |

> [!TIP]
> **Which should I use?** Stay with **Dynamic** unless you have a specific reason not to (such as needing hardware breakpoint debugging with GDB). Its main weakness (a larger payload) rarely matters, and it keeps working when the loader on the board and your installed core drift slightly apart.

### 4. Compile the Sketch

From the directory containing your new `led` folder, compile the sketch choosing one of the two link modes:

#### Option A: Dynamic Mode (Default)
Dynamic is used by default when no link mode flag is specified. It produces a relocatable extension (`.elf-zsk.bin`) that the Zephyr loader links at boot time:
```bash
arduino-cli compile -b arduino:zephyr:unoq --output-dir ./led/build ./led
```
* **Upload Artifact:** `./led/build/led.ino.elf-zsk.bin` (~74 KB)

#### Option B: Static Mode (Required for Hardware Breakpoints & Debugging)
To compile in Static mode, append `:link_mode=static` to the board FQBN. This resolves all symbols at build time to fixed flash addresses:
```bash
arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --output-dir ./led/build ./led
```
* **Upload Artifact:** `./led/build/led.ino.bin-zsk.bin` (~24 KB)

> [!IMPORTANT]
> **Upload the file that matches the mode you compiled with:**
> - **Dynamic:** upload `led.ino.elf-zsk.bin`.
> - **Static:** upload `led.ino.bin-zsk.bin`.
>
> **Both files are created in either mode**, so finding a file in `build/` doesn't make it valid. In a Dynamic build, the `.bin-zsk.bin` is just a raw dump of an unlinked object and **will not run**. In a Static build, the `.elf-zsk.bin` is not the upload artifact. If you switch between modes, clean the `build/` folder (`rm -rf ./led/build`) before recompiling to avoid flashing a stale payload.

* **Why `--output-dir`?** By default, `arduino-cli` places compiled binaries in a cache folder under your home directory (`~/.cache/arduino/sketches/...`, or `/root/.cache/...` if run as root). This flag copies the output into `./led/build/`, making it easy to grab the correct upload file.

---

## Part 6: The Manual Upload (Direct OpenOCD Flashing via SSH)

Instead of `arduino-cli upload`, we copy the sketch payload to the board over SSH (`scp`) and flash it using the board's internal OpenOCD.

**Set the Board Address Variable:**
Set `BOARD_HOST` to your board's Ethernet-over-USB IP (`192.168.7.1`) or mDNS hostname (`uno-q.local`):
```bash
export BOARD_HOST="arduino@192.168.7.1"
```

**1. Create a Staging Folder on the Board**
Use `ssh` to create a temporary folder on the board's Linux side:
```bash
ssh ${BOARD_HOST} "mkdir -p /tmp/remoteocd"
```

> [!IMPORTANT]
> **One-Time Step (New Board or After Updating `arduino:zephyr` Core):**
> Flashing the Debian Linux OS with `arduino-flasher-cli` only programs the Qualcomm eMMC—it does **not** update the STM32 microcontroller's internal flash (`0x08000000`). When `arduino-cli core install/upgrade arduino:zephyr` installs a newer core release (such as `arduino:zephyr 1.1.0`, whose loader is `265,460` bytes vs. the factory `263,680`-byte loader), sketches compiled with the new core import updated LLEXT symbols (`pinctrl_configure_pins`, `__real_*` wrappers, and updated device ordinals) that the older factory loader does not export—causing the sketch to fail to load until the STM32's Zephyr loader is updated once:
> ```bash
> LOADER_BIN=$(ls /opt/arduino-uno-q-tools/data/packages/arduino/hardware/zephyr/*/firmwares/zephyr-arduino_uno_q_stm32u585xx.bin | sort -V | tail -n 1)
> scp "${LOADER_BIN}" ${BOARD_HOST}:/tmp/remoteocd/loader.bin
> ssh ${BOARD_HOST} "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/loader.bin 0x08000000 bin' -c 'reset halt' -c 'resume' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
> ```
> Once the STM32 has the matching Zephyr loader at `0x08000000`, all subsequent uploads only need Steps 2 and 3 below.

**2. Copy the Sketch Payload via `scp`**
Copy only your compiled sketch payload to the board:
```bash
# If compiled with Dynamic mode (default):
scp ./led/build/led.ino.elf-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin

# If compiled with Static mode:
# scp ./led/build/led.ino.bin-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin
```

> [!NOTE]
> **How big is the payload?** There is no 100 KB limit. For reference, the LED blink sketch above produces a **~76 KB** `.elf-zsk.bin`. Sketches that pull in libraries (e.g., `Arduino_RouterBridge`) will be larger. The real limits, defined by the core's `boards.txt`, are **768 KB (786,432 bytes) of flash** and **256 KB (262,144 bytes) of RAM** — `arduino-cli compile` prints your usage against these after every build. You can check the payload size with `ls -l ./led/build/*.elf-zsk.bin`.

**3. Flash the Microcontroller via `ssh`**
Run the board's internal OpenOCD over `ssh` to flash the STM32 microcontroller directly:
```bash
ssh ${BOARD_HOST} "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
```

### Understanding the Flash Command
Because the Arduino UNO Q has two processors (a Linux processor and an STM32 microcontroller), this command runs *on the Linux processor* to directly program the STM32. The sequence mirrors the sketch-flashing steps of the core's official `flash_sketch.cfg` script:

*   **`/opt/openocd/bin/openocd`**: The OpenOCD executable residing on the board's Linux filesystem.
*   **`-d2`**: Sets the debug output level to `2` (Info). This suppresses overly verbose debugging logs while still showing critical success/failure messages.
*   **`-s /opt/openocd ...`**: The `-s` flags add specific directories to OpenOCD's search path so it can locate necessary scripts and configuration files.
*   **`-f openocd_gpiod.cfg`**: Loads the board-specific hardware interface configuration. This tells OpenOCD how the Linux processor should use its GPIO pins to "bitbang" the SWD (Serial Wire Debug) protocol to communicate with the STM32 microcontroller.
*   **`-c 'init'`**: Initializes OpenOCD, connects to the STM32 via SWD, and prepares it for commands.
*   **`-c 'reset halt'`**: Resets the STM32 and immediately halts its CPU so the flash can be written from a known, stable state.
*   **`-c 'flash write_image erase ... 0x8100000 bin'`**: Erases the required sectors and writes the sketch payload starting at `0x08100000` — the start of the STM32U585's second flash bank, which is the core's configured sketch address (`unoq.upload.address`). The Zephyr loader lives below this address and is not touched.
*   **`-c 'reset halt'` & `-c 'resume'`**: Resets the STM32 and resumes execution from the updated flash while preserving the firmware's DWT/DebugMonitor setup.
*   **`-c 'sleep 100'`**: Pauses for 100 milliseconds so the Zephyr loader completes its initial clear of the backup SRAM mailbox (`0x40036400`).
*   **`-c 'mww 0x40036400 0xCAFFEEEE'`**: "Memory Write Word" — writes `0xCAFFEEEE` to backup SRAM at `0x40036400` after the 100 ms delay to signal the loader that a fresh sketch upload completed.
*   **`-c 'mww 0xE000EDF0 0xA05F0000'`**: Clears `DHCSR.C_DEBUGEN` (using the Cortex-M `DBGKEY` `0xA05F0000`) left set by OpenOCD so DWT null-pointer traps raise `DebugMonitor` rather than halting the CPU when no debugger is attached.
*   **`-c 'shutdown'`**: Closes OpenOCD and returns control to your WSL terminal.

> [!NOTE]
> The official `flash_sketch.cfg` also sets `reset_config srst_only srst_nogate srst_push_pull connect_assert_srst` before `init`. The command above does not set it explicitly. If you ever see unreliable connects or resets, adding `-c 'reset_config srst_only srst_nogate srst_push_pull connect_assert_srst'` before `-c 'init'` matches the official behaviour more closely.

---

## Part 7: VSCode Integration via SSH (Dynamic Link Mode & Unified Tasks)

With the board accessible over standard IP (either via **Ethernet-over-USB** at `192.168.7.1` or via **Wi-Fi** at `uno-q.local`), you can integrate compilation and one-step SSH uploading directly into VSCode using the default **Dynamic link mode**.

*(If you need hardware breakpoint debugging with GDB, see [Appendix 4: VSCode Static Link Mode & Hardware Breakpoint Debugging via SSH](#appendix-4-vscode-static-link-mode--hardware-breakpoint-debugging-via-ssh)).*

### 1. Configure Passwordless SSH (One-Time Setup)

In your WSL terminal, generate an SSH key if you don't already have one, and copy your public key to the board so VSCode build/upload tasks run without password prompts:

```bash
# Generate an SSH key if you do not already have one:
[ -f ~/.ssh/id_ed25519 ] || ssh-keygen -t ed25519

# If you connect via Ethernet-over-USB:
ssh-copy-id arduino@192.168.7.1

# If you connect via Wi-Fi:
ssh-copy-id arduino@uno-q.local
```

### 2. Open the `/workspace/arduino-uno-q` Folder in VSCode (Remote Development)

> [!IMPORTANT]
> **Install the Remote Development Extension First:**
> Before opening the WSL workspace in VSCode, open the **Extensions** view (`Ctrl + Shift + X`) in VSCode on Windows and install the **Remote Development** extension pack (`ms-vscode-remote.vscode-remote-extensionpack`)—or at minimum the **WSL** extension (`ms-vscode-remote.remote-wsl`).

1. **Connect to WSL using the Remote Extension & Open `/workspace/arduino-uno-q`:**
   * Click the **Open a Remote Window** button (`><`) in the bottom-left corner of VSCode (or press `Ctrl + Shift + P` and select **WSL: Connect to WSL in New Window** / **WSL: Connect to WSL using Distro...**).
   * Once connected to your WSL instance, go to **File $\rightarrow$ Open Folder...** (`Ctrl + K, Ctrl + O`), type `/workspace/arduino-uno-q`, and click **OK**.
   * *(Alternatively, from your WSL terminal inside `/workspace/arduino-uno-q`, simply run `code /workspace/arduino-uno-q`).*
2. **Turn Off Restricted Mode (Trust the Workspace):**
   * When VSCode opens the folder, click **Yes, I trust the authors** in the Workspace Trust dialog.
   * If the folder opens in **Restricted Mode**, click the **Restricted Mode** indicator in the top banner or bottom status bar, select **Manage**, and click **Trust Folder**. *(VSCode disables `.vscode/tasks.json` execution while Restricted Mode is active).*

### 3. Configure VSCode Tasks (`.vscode/tasks.json`)

Create or update `.vscode/tasks.json` in your `/workspace/arduino-uno-q` workspace. This configuration compiles the currently open sketch in **Dynamic link mode** (`arduino:zephyr:unoq`) and uses `scp` and `ssh` to flash the `.elf-zsk.bin` payload onto the microcontroller:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Dynamic)",
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
            "label": "Arduino UNO Q: Upload via SSH (Dynamic)",
            "type": "shell",
            "command": "/opt/arduino-uno-q-tools/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\" && ssh arduino@192.168.7.1 \"mkdir -p /tmp/remoteocd\" && scp \"${fileDirname}/build/\"*.elf-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin && ssh arduino@192.168.7.1 \"/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'\"",
            "group": {
                "kind": "test",
                "isDefault": true
            },
            "presentation": {
                "reveal": "always",
                "panel": "new"
            },
            "problemMatcher": []
        }
    ]
}
```
*(Note: If using Wi-Fi instead of Ethernet-over-USB, simply change `arduino@192.168.7.1` to `arduino@uno-q.local`).*

### 4. Running Build & Upload Tasks in VSCode
1. Open your `.ino` sketch file in VSCode (e.g., `led/led.ino`).
2. **To Compile Only:** Press **`Ctrl + Shift + B`** (runs the default build task `Arduino UNO Q: Compile Current Sketch (Dynamic)`).
3. **To Compile & Upload:** Open the Command Palette (`Ctrl + Shift + P`), select **Tasks: Run Task**, and choose **`Arduino UNO Q: Upload via SSH (Dynamic)`**.

---

## Appendix 1: The `ZlpAwareHost="0"` Fix & Compatibility with Future `qdl` Releases

#### A. How Qualcomm Firehose + USB Bulk Transfers Work
When `arduino-flasher-cli` flashes the Arduino UNO Q, it extracts its embedded `qdl` (`v2.4-dirty`) binary into a temporary directory (`/tmp/qdl-*/qdl`) and communicates with the board's Qualcomm QRB2210 boot ROM (`05c6:9008`) in two phases:
1. **Sahara Protocol:** Uploads the Firehose programmer (`prog_firehose_ddr.elf`) into the processor's RAM and starts it.
2. **Firehose Protocol:** Sends XML commands (`<configure>`, `<program>`, `<patch>`, `<power>`) interleaved with raw binary partition transfers (`rawmode="true"`).

In USB 2.0 High-Speed Bulk mode, the maximum packet size on the wire (`wMaxPacketSize`) is **512 bytes**, while Firehose negotiates a DMA transfer buffer size of **1 MiB (`MaxPayloadSizeToTargetInBytes = 1,048,576` bytes = `2,048` sectors)** per chunk.

Per the USB 2.0 specification, a bulk transfer queued for up to `1,048,576` bytes completes only when **one of two conditions** is met:
1. The host transmits the full `1,048,576` bytes (`2,048` full 512-byte packets), **or**
2. The host transmits a packet **shorter than 512 bytes**—such as a **0-byte Zero-Length Packet (ZLP)**—to signal to the target's USB DMA controller that no further 512-byte packets belong to that transfer.

#### B. Why `xbl_a` Hung at Exactly `33%` Over `usbipd-win`
When `qdl` programs the first partition (`xbl_a` from `xbl.elf`, `3,168,464` bytes $\rightarrow$ padded to `6,189` sectors = `3,168,768` bytes), `firehose_program()` sends 4 chunks:

| Chunk | Sectors | Bytes Sent | Cumulative Progress | Behavior Over `usbipd-win` |
| :--- | :--- | :--- | :--- | :--- |
| **Chunk 1** | `2,048` | `1,048,576` (1 MiB) | `2048 / 6189` = **`33.09%`** | Fills the board's full 1 MiB DMA buffer; terminal prints **`33%`**. |
| **Chunk 2** | `2,048` | `1,048,576` (1 MiB) | `4096 / 6189` = `66.18%` | Fills the board's full 1 MiB DMA buffer (`< 100 ms`, so 10 Hz UI refresh skipped). |
| **Chunk 3** | `2,048` | `1,048,576` (1 MiB) | `6144 / 6189` = `99.27%` | Fills the board's full 1 MiB DMA buffer (`< 100 ms`, so 10 Hz UI refresh skipped). |
| **Chunk 4** | **`45`** | **`23,040`** | `6189 / 6189` = `100.00%` | **Shorter than 1 MiB (`23,040 < 1,048,576`), AND an exact multiple of 512 bytes (`45 × 512`).** |

At the end of **Chunk 4** (`23,040` bytes):
1. Because `23,040 % 512 == 0`, `qdl`'s `usb_write()` calls `libusb_bulk_transfer(usb_handle, out_ep, NULL, 0, &actual, timeout)` to send a **0-byte ZLP**.
2. WSL2's `vhci_hcd` driver forwards that 0-length URB over TCP to `usbipd-win` on Windows.
3. Due to [`dorssel/usbipd-win#924`](https://github.com/dorssel/usbipd-win/issues/924) (*Dropped bulk transfer ZLP writes through WSL2*), `usbipd-win` passes `urb.buf = NULL` and `urb.len = 0` to `VBoxUSB.sys`, which **silently drops 0-length URBs** and never sends a ZLP onto the physical USB wire.
4. When the board's USB controller is in **ZLP mode (`zlp_mode = TRUE`)**, its hardware DMA engine sits at `23,040 / 1,048,576` bytes waiting indefinitely for the dropped ZLP, while `qdl` waits 30 seconds in `firehose_read()` for `<response value="ACK" rawmode="false"/>` before aborting with `flashing of xbl_a failed`.

#### C. How `ZlpAwareHost="0"` Solves This on the Board
Inspecting the strings inside the board's Firehose programmer (`prog_firehose_ddr.elf`) shows that Qualcomm provides a configuration attribute specifically for host USB stacks that cannot transmit ZLPs:

```text
ZlpAwareHost
Calling usb_al_bulk_set_zlp_mode(FALSE), zlp is set to %d   ("host lacks ZLP")
Calling usb_al_bulk_set_zlp_mode(TRUE), zlp is not set to %d ("host sends ZLP")
```

* When **ZLP mode is `TRUE`** (default), the board programs its Synopsys DWC3 USB RX Transfer Request Block (TRB) for the full `MaxPayloadSizeToTargetInBytes` (`1,048,576` bytes) and waits for a 0-byte ZLP to terminate the final `23,040`-byte chunk early.
* When **ZLP mode is `FALSE`** (`ZlpAwareHost="0"`), the board calls `usb_al_bulk_set_zlp_mode(FALSE)` and programs the final USB RX TRB to the **exact remaining sector byte count** (`45 × 512 = 23,040` bytes). As soon as the 45th 512-byte packet arrives, the TRB is 100% full, the DMA transfer completes immediately **without needing a ZLP**, the board writes `xbl_a` to eMMC, and it immediately replies with `<response value="ACK" rawmode="false"/>`.

#### D. Why All Three `sed` Substitutions Are Needed
Disassembling `qdl` (`v2.4-dirty`) embedded inside `arduino-flasher-cli` reveals a casing mismatch in `qdl` (`firehose.c`, `firehose_send_configure`):
```c
xml_setpropf(node, "ZLPAwareHost", "%d", 1);
```
`qdl` spells the XML attribute **`ZLPAwareHost`** (with uppercase **`LP`**), whereas `prog_firehose_ddr.elf` on the Arduino UNO Q uses a **case-sensitive** comparison against **`ZlpAwareHost`** (with lowercase **`lp`**):
* Sending `ZLPAwareHost="0"` (uppercase `LP`) is ignored by the board's parser:
  `DEBUG: Calling usb_al_bulk_set_zlp_mode(TRUE), zlp is not set to 0`
* Sending `ZlpAwareHost="0"` (lowercase `lp`) is recognized and disables ZLP mode:
  `DEBUG: Calling usb_al_bulk_set_zlp_mode(FALSE), zlp is set to 0`

The `sed` command in Step 1.3 saves a backup of the unpatched binary at `/opt/arduino-uno-q-tools/flasher/arduino-flasher-cli.bak` and patches three exact places inside the embedded `qdl` binary:
```bash
sudo LC_ALL=C sed -i.bak \
  -e 's/ZLPAwareHost/ZlpAwareHost/g' \
  -e 's/\x48\x89\xdf\xb9\x01\x00\x00\x00\(\xba....\xbe....\x31\xc0\xe8\)/\x48\x89\xdf\xb9\x00\x00\x00\x00\1/g' \
  -e 's/\x48\x85\xd2\x74\x18\x48\x83\xc4\x28\x89\xe8/\x48\x85\xd2\x90\x90\x48\x83\xc4\x28\x89\xe8/g' \
  /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli
```
1. **`-e 's/ZLPAwareHost/ZlpAwareHost/g'`**  
   Fixes the string literal in `.rodata` (`0xCD00B`) from `"ZLPAwareHost"` to `"ZlpAwareHost"` so `prog_firehose_ddr.elf` recognizes the attribute and calls `usb_al_bulk_set_zlp_mode(FALSE)`.
2. **`-e 's/\x48\x89\xdf\xb9\x01\x00\x00\x00\(\xba....\xbe....\x31\xc0\xe8\)/\x48\x89\xdf\xb9\x00\x00\x00\x00\1/g'`**  
   In `firehose_send_configure()` (`0x4D21`), changes `mov ecx, 1` (`b9 01 00 00 00`) to `mov ecx, 0` (`b9 00 00 00 00`) before calling `xml_setpropf(node, "ZlpAwareHost", "%d", ecx)`, so `qdl` sends `ZlpAwareHost="0"` (using `....` wildcards for the string pointers so the regex still matches if `qdl` is recompiled).
3. **`-e 's/\x48\x85\xd2\x74\x18\x48\x83\xc4\x28\x89\xe8/\x48\x85\xd2\x90\x90\x48\x83\xc4\x28\x89\xe8/g'`**  
   In `usb_write()` (`0xA3C1`), replaces `jz +0x18` (`74 18`, which jumps to the 0-byte `libusb_bulk_transfer(..., NULL, 0)` ZLP call whenever `len_orig % 512 == 0`) with `nop; nop` (`90 90`), avoiding redundant 0-byte URB submissions over `usbipd-win`.

#### E. Will This Patch Work if `qdl` Is Updated in a Future Release?
1. **If `arduino-flasher-cli` updates its Go wrapper but keeps the same `qdl` build:**
   * **Yes, 100%.** Because `sed` searches by byte pattern rather than fixed file offsets, shifting the offset where `qdl` is embedded has no effect.
2. **If a future release recompiles or upgrades the embedded `qdl` binary:**
   * **Expression 1 (`s/ZLPAwareHost/ZlpAwareHost/g`):** Continues to work as long as upstream `linux-msm/qdl` keeps the `"ZLPAwareHost"` string literal.
   * **Expression 2 (`\x48\x89\xdf\xb9\x01\x00\x00\x00\(\xba....\xbe....\x31\xc0\xe8\)`):** Because we used `....` wildcards and a `sed` capture group `\1` instead of hardcoding the `.rodata` memory addresses of `"%d"` (`0x004EB09D`) and `"ZLPAwareHost"` (`0x004CD00B`), this regex **survives `qdl` recompilations** where string addresses shift.
   * **Expression 3 (`\x48\x85\xd2\x74\x18\x48\x83\xc4\x28\x89\xe8`):** Contains no memory addresses (only register opcodes: `test rdx, rdx; jz +0x18; add rsp, 0x28; mov eax, ebp`), so it survives minor rebuilds with the same compiler. If a future compiler version changes register allocation or stack frame size in `usb_write()`, Expression 3 might not match—however, **Expressions 1 + 2 alone are sufficient** for flashing to succeed (once `ZlpAwareHost="0"` is sent, the board ignores `usbipd-win`'s dropped ZLPs anyway).
   * **How to verify after upgrading `arduino-flasher-cli`:**
     ```bash
     cmp -l /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli.bak /opt/arduino-uno-q-tools/flasher/arduino-flasher-cli
     ```
     This should report **5 changed bytes** (2 bytes for `LP` $\rightarrow$ `lp`, 1 byte for `01` $\rightarrow$ `00`, and 2 bytes for `74 18` $\rightarrow$ `90 90`).

---

## Appendix 2: The `qcserial` Driver & Impact on Normal Operation

#### A. Why `qcserial` Breaks EDL Flashing
When the Arduino UNO Q is connected in EDL mode, it enumerates under Qualcomm's Emergency Download USB ID (`05c6:9008`). By default, the Linux kernel's `qcserial` module (`Qualcomm USB modem converter`) matches `05c6:9008` in its modalias table and claims USB Interface 0 to create `/dev/ttyUSB0`. Because `qcserial` holds the USB bulk endpoints open, `qdl` (`libusb`) cannot communicate cleanly with the Sahara/Firehose programmer.

Furthermore, a simple `blacklist qcserial` line in `/etc/modprobe.d/blacklist-qcserial.conf` is not enough on its own: whenever the board resets and re-attaches via `usbipd-win`, `udev` invokes `modprobe` using the USB device's modalias, which bypasses `blacklist` unless `install qcserial /bin/false` is also present.

#### B. Does Disabling `qcserial` Affect Normal Operation?
**No — disabling `qcserial` has zero impact on the Arduino UNO Q or standard microcontroller boards under normal operation:**
1. **Normal Arduino UNO Q Operation Uses Different Drivers (`cdc_acm` & `usbfs`):**
   When booted normally (without the EDL jumper), the UNO Q enumerates under Arduino's USB ID (`2341:0078`) as a composite USB device:
   * **Serial Monitor (`/dev/ttyACM0`):** Handled by the standard Linux **`cdc_acm`** kernel driver (not `qcserial`).
   * **ADB & Sketch Uploading (`adb` / `arduino-cli`):** Communicates directly over USB via **`usbfs` / `libusb`** (not `qcserial`).
2. **Other Development Boards (STM32, ESP32, Arduino Uno R3/R4, FTDI, CH340, CP210x):**
   Standard USB-to-UART bridges and native USB microcontrollers use `cdc_acm`, `ftdi_sio`, `ch341`, or `cp210x`—none of them use `qcserial`.
3. **When Would `qcserial` Ever Be Needed?**
   `qcserial` is a legacy driver written for older **Qualcomm Gobi 2000/3000 3G/LTE cellular WWAN modem cards/dongles** that exposed AT-command serial ports over `/dev/ttyUSB*`. If you ever attach an old Qualcomm Gobi cellular modem to WSL and need `/dev/ttyUSB*`, you can re-enable the driver at any time with:
   ```bash
   sudo rm -f /etc/modprobe.d/blacklist-qcserial.conf
   sudo modprobe qcserial
   ```

---

## Appendix 3: Link Modes (Dynamic vs. Static Architecture & Mechanics)

The Arduino UNO Q microcontroller runs a pre-built **Zephyr loader** firmware shipped with the core. Your sketch is built as an add-on that this loader starts. Both modes start from the same compiled object files, but differ in the final link step, the upload file, and in what the loader has to do at boot:

* **Dynamic (default):** The sketch is built as a Zephyr LLEXT relocatable extension (`.elf-zsk.bin`). The Zephyr loader links it into RAM at boot time, resolving symbols by name. Tolerates minor loader revisions across core updates.
* **Static (`:link_mode=static`):** The sketch is fully linked at compile time to fixed physical flash addresses (`0x08100000`+) producing a `.bin-zsk.bin` payload. **Required for hardware breakpoint debugging with GDB and OpenOCD.**

#### A. Dynamic Link Mode (Default)
The sketch is built as a **Zephyr LLEXT (Linkable Loadable Extension)** — conceptually similar to a shared library (`.so`) on Linux.

1. **Compile-time check:** The build first does a trial link against `syms-dynamic.ld`, a list of the symbols the loader exports (Arduino API, libc, kernel calls, devices). This catches undefined symbols and size overflows during compilation instead of at boot.
2. **Partial link:** The real link uses `ld -r`, producing a *relocatable* ELF object. Function calls into the loader are left unresolved, and no final addresses are assigned (the `.text` section starts at `0x00000000`).
3. **Packaging:** `zephyr-sketch-tool` stamps a small header into the ELF's spare identification bytes (the file length plus Arduino's `0x2341` marker) and saves it as `<sketch>.ino.elf-zsk.bin`.
4. **At boot:** The loader reads that ELF directly from flash at `0x08100000` using Zephyr's LLEXT subsystem. It allocates memory from a dedicated heap (`CONFIG_LLEXT_HEAP_SIZE=256` KB), copies the code into **RAM**, resolves each symbol **by name** against its export table, applies relocations, and calls `setup()`/`loop()`. Read-only data is kept in flash (`CONFIG_LLEXT_RODATA_NO_RELOC`).

**Trade-offs:**
- ✅ **Tolerates loader rebuilds:** Symbols are matched by name, so the sketch keeps working if the loader is rebuilt and functions move to different addresses (as long as the exported API is unchanged).
- ✅ **Default and best supported:** This is what Arduino ships and tests.
- ❌ **Larger payload:** The file carries relocation and symbol tables (the LED sketch is ~74 KB).
- ❌ **Limited API:** Only symbols the loader explicitly exports can be called.
- ❌ **Code runs at boot-assigned RAM addresses:** The code's real addresses aren't known until the loader places it. This is why source-level breakpoints failed during earlier debugging attempts: the program was running at RAM addresses like `0x20015f58`, while the `.elf` listed its functions near `0x00000000`.

#### B. Static Link Mode (Required for Hardware Breakpoints & Debugging)
The sketch is **fully linked at build time** against fixed addresses inside the loader firmware.

1. **Full link:** The linker uses `syms-static.ld` (the absolute address of *every* public symbol in the loader, roughly twice as many as the dynamic list, including STM32 HAL and compiler-runtime functions), plus `memory-static.ld` and `build-static.ld`. There is no `-r`, so every call is resolved to a hard-coded address.
2. **Packaging:** The linked image is converted to a raw `.bin`, then `zephyr-sketch-tool -prelinked` ("Provided file has already been linked to Zephyr") adds its header, producing `<sketch>.ino.bin-zsk.bin`.
3. **At boot:** No relocation or symbol lookup is needed. The loader can start the pre-linked image directly.

**Trade-offs:**
- ✅ **Enables real-time hardware debugging:** Because the compiler generates an executable ELF (`EXEC`) with fixed flash addresses (`0x08100000`+) rather than a relocatable object (`ET_REL` at `0x00000000`), GDB and OpenOCD can plant hardware breakpoints directly on `setup()` and `loop()` and step through your source code line-by-line!
- ✅ **Smaller payload and faster start:** No symbol or relocation tables, and no load-time linking (LED sketch payload is 24,788 bytes vs 75,488 bytes).
- ✅ **Wider API:** It can call symbols the dynamic export list doesn't include.
- ❌ **Tied to one exact loader build:** Both symbol files say they were generated from a specific `zephyr.elf` (identified by a SHA256 hash). If the loader on the board isn't *exactly* that build, the hard-coded addresses point at the wrong code and the sketch will crash or behave unpredictably. This matters for the manual upload method in Part 6, which **never refreshes the loader**. *(Note: In physical board verification, the pre-installed loader on the UNO Q matched the core's loader with 0 code differences).*
- ❌ **Less common path:** It's an option rather than the default, so it gets less real-world use.

---

## Appendix 4: VSCode Static Link Mode & Hardware Breakpoint Debugging via SSH

To set hardware breakpoints and step through your sketch line-by-line in VSCode, compile and upload your sketch in **Static link mode** (`arduino:zephyr:unoq:link_mode=static`) so function symbols resolve to fixed physical flash addresses (`0x08100000`+).

### 1. Configure VSCode Tasks for Static Mode (`.vscode/tasks.json`)

Create or update `.vscode/tasks.json` in your workspace to compile with `:link_mode=static` and upload the `.bin-zsk.bin` artifact over SSH:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Static)",
            "type": "shell",
            "command": "/opt/arduino-uno-q-tools/arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --build-path \"${fileDirname}/build-static/tmp\" --output-dir \"${fileDirname}/build-static\" \"${fileDirname}\"",
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
            "label": "Arduino UNO Q: Upload via SSH (Static)",
            "type": "shell",
            "command": "/opt/arduino-uno-q-tools/arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --build-path \"${fileDirname}/build-static/tmp\" --output-dir \"${fileDirname}/build-static\" \"${fileDirname}\" && ssh arduino@192.168.7.1 \"mkdir -p /tmp/remoteocd\" && scp \"${fileDirname}/build-static/\"*.bin-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin && ssh arduino@192.168.7.1 \"/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'\"",
            "group": {
                "kind": "test",
                "isDefault": true
            },
            "presentation": {
                "reveal": "always",
                "panel": "new"
            },
            "problemMatcher": []
        }
    ]
}
```
*(Note: If using Wi-Fi instead of Ethernet-over-USB, simply change `arduino@192.168.7.1` to `arduino@uno-q.local`).*

### 2. Launch the OpenOCD Debug Server via SSH Tunnel

Before attaching the VSCode debugger, tunnel OpenOCD's GDB port (`3333`) over SSH from the board to your local machine.

In a **separate WSL terminal window**, run:

```bash
ssh -L 3333:localhost:3333 -N arduino@192.168.7.1 "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -c 'proc gdb_attach_hook {} { halt }' -c 'proc gdb_detach_hook {} { resume }' -f openocd_gpiod.cfg"
```
*Leave this terminal window running while debugging.*

### 3. Create the VSCode Debug Configuration (`.vscode/launch.json`)

Configure `.vscode/launch.json` to attach to the SSH-tunneled GDB server using `led.ino_debug.elf` generated in `build-static/`:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Arduino UNO Q: Hardware Debug (SSH)",
            "cwd": "${workspaceFolder}/led",
            "executable": "${workspaceFolder}/led/build-static/led.ino_debug.elf",
            "request": "attach",
            "type": "cortex-debug",
            "servertype": "external",
            "gdbTarget": "localhost:3333",
            "gdbPath": "/usr/bin/gdb-multiarch",
            "showDevDebugOutput": "raw",
            "postAttachCommands": []
        }
    ]
}
```

### 4. Debugging Workflow
1. Run the **`Arduino UNO Q: Upload via SSH (Static)`** task (`Ctrl + Shift + P` -> **Tasks: Run Task**).
2. Ensure the OpenOCD SSH tunnel terminal from Step 2 is running.
3. Open `led/led.ino` and set a breakpoint by clicking in the left margin next to a line inside `loop()` or `setup()`.
4. Press **`F5`** (or click the green Play button in the Run & Debug view).
5. VSCode attaches to the target over the network. Execution halts immediately at your breakpoint! You can inspect local and global variables, view call stacks (`loop()` called from `main()`), and use Step Over (`F10`) or Step Into (`F11`) to walk through your code line-by-line.

---

## Appendix 5: Verified Facts For Compiled Binary

Values below were read and verified against the installed `arduino:zephyr` (`1.1.0`) core and real physical hardware testing on the Arduino UNO Q.

| Item | Value | Source |
|---|---|---|
| Default upload artifact | `{project}.elf-zsk.bin` | `platform.txt`: `upload.extension=elf-zsk.bin` |
| Static-mode upload artifact | `{project}.bin-zsk.bin` | `boards.txt`: `unoq.menu.link_mode.static.upload.extension` |
| LED sketch payload size (Dynamic) | 77,820 bytes (~76 KB) `.elf-zsk.bin` | `led/build/` listing (`1.1.0`) |
| LED sketch payload size (Static) | 24,788–29,912 bytes (~24–29 KB) `.bin-zsk.bin` | `led/build/` listing |
| Static debug ELF type | `EXEC (Executable file)`, Entry point: `0x08100011` | `readelf -h led.ino_debug.elf` |
| Static flash addresses | `setup()` at `0x08102590`, `loop()` at `0x0810259c` | `readelf -sW led.ino_debug.elf` |
| On-board loader match | Factory `0.5x` loader (`263,680` bytes) vs `1.1.0` loader (`265,460` bytes) at `0x08000000` | `dump_image` vs `zephyr-*.bin` |
| Hardware Breakpoints | **Verified working!** Breakpoints hit, call stack traced, and single-stepping confirmed via GDB | Physical board test (GDB + OpenOCD) |
| Static link mode flags | `build.link_mode=static`, links with `syms-static.ld`, packaged with `zephyr-sketch-tool -prelinked` | `boards.txt` / `platform.txt` |
| Dynamic link mode flags | Relocatable link (`build-dynamic.ld -r`) against `syms-dynamic.ld`, ELF type `ET_REL` | `platform.txt` / ELF header of `.elf-zsk.bin` |
| Loader LLEXT settings | `CONFIG_LLEXT=y`, `CONFIG_LLEXT_TYPE_ELF_OBJECT=y`, `CONFIG_LLEXT_HEAP_SIZE=256`, `CONFIG_LLEXT_RODATA_NO_RELOC=y` | `firmwares/zephyr-arduino_uno_q_stm32u585xx.config` |
| Max sketch flash size | 786,432 bytes (768 KB) | `boards.txt`: `unoq.upload.maximum_size` |
| Max sketch RAM | 262,144 bytes (256 KB) | `boards.txt`: `unoq.upload.maximum_data_size` |
| Sketch flash address | `0x08100000` | `boards.txt`: `unoq.upload.address` |
| Official upload tool | `remoteocd` (passes loader + sketch over ADB) | `boards.txt` / `platform.txt` |
| Loader file passed to `remoteocd` | `zephyr-arduino_uno_q_stm32u585xx.elf`, 2,346,964 bytes | `firmwares/` listing (`1.1.0`) |
| Loader flashed content | `zephyr-arduino_uno_q_stm32u585xx.bin`, 265,460 bytes | `firmwares/` listing (`1.1.0`) |
| Loader/sketch rewrite policy | Each rewritten only if `flash verify_image` fails | `flash_sketch.cfg` |
| Post-flash sequence | `reset halt`, `resume`, `sleep 100`, `mww 0x40036400 0xCAFFEEEE`, `mww 0xE000EDF0 0xA05F0000`, `shutdown` | `flash_sketch.cfg` (`1.1.0`) |
