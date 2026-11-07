# Arduino UNO Q: Native `arduino-flasher-cli` & `arduino-cli` Setup on macOS & Windows

This guide adapts **Parts 1, 4 and 5** of the [WSL & USBIPD Upload Guide](uno_q_wsl_upload_guide.md) so the OS image flasher (`arduino-flasher-cli`) and `arduino-cli` run **directly on macOS and Windows**, with no WSL. It covers installing the flasher and flashing an image (Part 1, section A), installing and configuring `arduino-cli` (Part 1, section B), compiling (Part 4) and uploading a sketch over SSH (Part 5).

Everything is installed into a single, self-contained tools folder:

| macOS | Windows |
|---|---|
| `/Tools/TC1004B` | `C:\Tools\TC1004B` |

That folder holds `arduino-flasher-cli` and the local Debian OS images you flash with it, plus the `arduino-cli` program, its configuration file, the Zephyr core, the compilers and other tools the core downloads, libraries, downloaded archives and the build cache. It plays the same role as `/opt/arduino-uno-q-tools` in the WSL guide.

> [!NOTE]
> **Scope:** This guide covers installing `arduino-flasher-cli` and using it to flash a local OS image onto the board, and installing and setting up `arduino-cli` for the UNO Q's Zephyr core, compiling sketches and uploading them to the board over SSH. ADB and debugging are not covered here.

> [!NOTE]
> **Why not Homebrew or winget?** Arduino doesn't publish `arduino-flasher-cli` on Homebrew or winget. For `arduino-cli`, Homebrew can't install a single package into a custom folder, and winget's package is a system-wide MSI installer. To keep *everything* inside `TC1004B`, this guide uses Arduino's official GitHub downloads for both tools. On macOS, `arduino-cli` comes from the official [install script](https://docs.arduino.cc/arduino-cli/installation). The same commands also update each tool.

---

## Folder Layout

After completing this guide, the folder looks like this (shown with `/`; on Windows the same layout sits under `C:\Tools\TC1004B`):

```
/Tools/TC1004B/
├── flasher/
│   ├── arduino-flasher-cli                          # OS image flasher (arduino-flasher-cli.exe on Windows)
│   └── arduino-unoq-debian-image-<VERSION>.tar.zst  # local OS images to flash onto the board
├── arduino-cli            # the CLI program (arduino-cli.exe on Windows)
├── arduino-cli.yaml       # configuration file (ARDUINO_CONFIG_FILE points here)
├── data/                  # cores + toolchains (data/packages/arduino/...): Zephyr core, compilers, tools
├── downloads/             # downloaded core/library archives
├── user/                  # sketchbook + installed libraries (user/libraries)
└── cache/                 # build cache
```

---

## What Changes Compared to WSL

| | WSL (Ubuntu) | macOS (native) | Windows (native) |
|---|---|---|---|
| Tools folder | `/opt/arduino-uno-q-tools` | `/Tools/TC1004B` | `C:\Tools\TC1004B` |
| Flasher folder | `/opt/arduino-uno-q-tools/flasher` | `/Tools/TC1004B/flasher` | `C:\Tools\TC1004B\flasher` |
| Flasher download | `linux-amd64.tar.gz` | `darwin-arm64` (Apple Silicon) / `darwin-amd64` (Intel, if published) `.tar.gz` | `windows-amd64` archive |
| Flasher extras | `qcserial` blacklist + `ZlpAwareHost` patch + `usbipd` | **None**: no patch, no driver blacklist | EDL USB driver, installed by the flasher on first run (Administrator) |
| `arduino-cli` install method | `install.sh` with `BINDIR` | `install.sh` with `BINDIR` (same) | Official `.zip` from GitHub releases |
| `PATH` + `ARDUINO_CONFIG_FILE` set in | `~/.bashrc` | `~/.zshrc` | User environment variables |
| Creating a top-level folder | `sudo mkdir` | Needs `/etc/synthetic.conf` + reboot (see below) | `New-Item` |
| UNO Q serial port (normal boot) | `/dev/ttyACM0` | `/dev/cu.usbmodem…` | `COMx` |
| USB passthrough | `usbipd-win` required | Not needed | Not needed |

The board's USB ID in normal operation is `2341:0078`. Compiling needs no board connection at all.

---

## Part 1: macOS (Apple Silicon & Intel)

All commands run in **Terminal** (zsh, the macOS default shell).

### 1. Create `/Tools/TC1004B`

Since macOS Catalina, the root of the startup disk (`/`) is a **read-only system volume**, so `sudo mkdir /Tools` fails with `Read-only file system`. Apple's supported way to add a top-level folder is `/etc/synthetic.conf`. It makes `/Tools` appear at boot as a link to a real folder on the writable Data volume.

1. Create the real folder on the Data volume and make it yours, so nothing later needs `sudo`:
   ```bash
   sudo mkdir -p /System/Volumes/Data/Tools/TC1004B
   sudo chown -R "$USER" /System/Volumes/Data/Tools
   ```
2. Register `/Tools` in `/etc/synthetic.conf` (only once). The separator **must be a tab**, which `printf '\t'` guarantees:
   ```bash
   grep -qE '^Tools[[:space:]]' /etc/synthetic.conf 2>/dev/null || \
     printf 'Tools\tSystem/Volumes/Data/Tools\n' | sudo tee -a /etc/synthetic.conf
   ```
3. **Restart the Mac.** `/Tools` appears after the reboot.
4. Confirm:
   ```bash
   ls -ld /Tools /Tools/TC1004B     # /Tools -> System/Volumes/Data/Tools
   touch /Tools/TC1004B/.write-test && rm /Tools/TC1004B/.write-test && echo "writable"
   ```

> [!TIP]
> To undo this later, delete the `Tools` line from `/etc/synthetic.conf`, restart, then remove `/System/Volumes/Data/Tools`.

### 2. Set `PATH` and the Config File Location

Add both tool locations to your `PATH` once, so both tools work from any folder after they're installed:

```bash
echo 'export PATH="/Tools/TC1004B:/Tools/TC1004B/flasher:$PATH"' >> ~/.zshrc
echo 'export ARDUINO_CONFIG_FILE="/Tools/TC1004B/arduino-cli.yaml"' >> ~/.zshrc
source ~/.zshrc
```

### 3. Install `arduino-flasher-cli` into `/Tools/TC1004B/flasher`

The flasher is published for macOS as `arduino-flasher-cli-<version>-darwin-arm64.tar.gz` (Apple Silicon). Intel Macs need a `darwin-amd64` asset, so check the [releases page](https://github.com/arduino/arduino-flasher-cli/releases) to confirm one is published for the version you want. It requires **macOS 12 (Monterey) or newer**.

1. Download the latest release for your Mac and place the binary in `/Tools/TC1004B/flasher`. Run the same block again any time to **update** it.
   ```bash
   mkdir -p /Tools/TC1004B/flasher

   ARCH=$(uname -m); [ "$ARCH" = "x86_64" ] && ARCH=amd64     # arm64 on Apple Silicon
   FLASHER_URL=$(curl -fsSL https://api.github.com/repos/arduino/arduino-flasher-cli/releases/latest \
     | grep -oE "https://[a-zA-Z0-9./_-]+darwin-${ARCH}\.tar\.gz" | head -1)
   echo "Downloading: ${FLASHER_URL}"

   # Extract into a temporary folder, then keep only the binary
   TMP=$(mktemp -d)
   curl -fsSL "${FLASHER_URL}" | tar -xz -C "$TMP"
   find "$TMP" -type f -name arduino-flasher-cli -exec mv -f {} /Tools/TC1004B/flasher/ \;
   rm -rf "$TMP"

   chmod +x /Tools/TC1004B/flasher/arduino-flasher-cli
   xattr -d com.apple.quarantine /Tools/TC1004B/flasher/arduino-flasher-cli 2>/dev/null || true
   ```
   If `FLASHER_URL` comes back empty, no asset exists for your architecture in the latest release. Download it manually from the releases page, then extract the binary into `/Tools/TC1004B/flasher`.
2. Verify, then list the available OS image versions:
   ```bash
   which arduino-flasher-cli     # /Tools/TC1004B/flasher/arduino-flasher-cli
   arduino-flasher-cli --help
   arduino-flasher-cli list
   ```
3. **Flash a local OS image onto the board.** This writes a Debian OS image that's already on your Mac to the UNO Q's internal eMMC storage. The image is a `.tar.zst` archive, for example one you downloaded earlier or a configured image made with Part 7 of the WSL guide. **Flashing erases everything on the board.**
   1. Copy the image into the flasher folder (adjust the source path to wherever your image is):
      ```bash
      cp ~/Downloads/arduino-unoq-debian-image-<VERSION>.tar.zst /Tools/TC1004B/flasher/
      ls -lh /Tools/TC1004B/flasher/*.tar.zst
      ```
   2. **Apple Silicon laptops only:** under **System Settings › Privacy & Security › Allow accessories to connect**, choose **Automatically When Unlocked** or **Always**. Otherwise, the board may never reach the flasher.
   3. Put the board into **EDL (Emergency Download) mode**. Unplug the USB-C cable, short **JCTL pin 9 (`USB_BOOT`) to pin 10 (`GND`)** with a jumper wire, then plug the cable back in with the jumper still in place.

      ![Arduino UNO Q EDL Jumper Location](C:/Users/nxky0/.gemini/antigravity/brain/d80e0346-a00c-4d23-b268-4ba7e521cb63/uno_q_edl_jumper.png)
   4. Confirm macOS sees the board as a Qualcomm EDL device (`QUSB_BULK…`, vendor `0x05c6`, product `0x9008`):
      ```bash
      ioreg -p IOUSB -l -w0 | grep -iE 'QUSB|"idVendor" = 1478'    # 1478 = 0x05c6 (Qualcomm)
      ```
   5. Flash the local image. This takes several minutes, so don't unplug the board:
      ```bash
      arduino-flasher-cli flash unoq /Tools/TC1004B/flasher/arduino-unoq-debian-image-<VERSION>.tar.zst --root-size 20GB
      # Example:
      # arduino-flasher-cli flash unoq /Tools/TC1004B/flasher/arduino-unoq-debian-image-20260528-558.tar.zst --root-size 20GB
      ```
      `--root-size` must be at least `10GB` and must fit your board. `20GB` matches the 32 GB UNO Q used in the WSL guide, so on a 16 GB board use a smaller value or omit the flag.
   6. When the flasher reports success, unplug the board, **remove the jumper**, and plug it back in to boot the new OS.

   > [!WARNING]
   > * **Skip the WSL-only steps:** no `usbipd` attach, no `qcserial` blacklist and, above all, **no `ZlpAwareHost` `sed` patch**. That patch works around a `usbipd-win` bug and targets the Linux binary, so applying it to the macOS binary would corrupt it.
   > * **After a failed attempt, always power-cycle the board** (unplug and replug with the jumper in place) before retrying.
   > * **`unable to open USB device` / stuck at `Waiting for EDL device`:** Use a data-capable USB-C cable plugged directly into the Mac (no hub or dock). If it still fails, retry once with `sudo /Tools/TC1004B/flasher/arduino-flasher-cli flash unoq …` to rule out USB permission issues.

> [!NOTE]
> * Always run the flasher from Terminal. Double-clicking it in Finder only shows a security warning.
> * The `xattr` line clears the Gatekeeper quarantine flag, which macOS adds to files downloaded with a browser. It's harmless if the flag isn't there.
> * `sudo` is normally not needed on macOS. If you ever do need it, use the full path: `sudo /Tools/TC1004B/flasher/arduino-flasher-cli …`.

### 4. Install `arduino-cli` into `/Tools/TC1004B`

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/Tools/TC1004B sh

which arduino-cli     # /Tools/TC1004B/arduino-cli
arduino-cli version
```

The script detects Apple Silicon or Intel and downloads the matching official build. Run the same command again any time to **update** to the latest release.

> [!IMPORTANT]
> If you installed `arduino-cli` with Homebrew earlier, remove it so there's only one copy: `brew uninstall arduino-cli`.

### 5. Configure `arduino-cli` & Install the Zephyr Core

```bash
mkdir -p /Tools/TC1004B/{data,downloads,user,cache}

arduino-cli config init --dest-dir /Tools/TC1004B --overwrite
arduino-cli config set directories.data      /Tools/TC1004B/data
arduino-cli config set directories.downloads /Tools/TC1004B/downloads
arduino-cli config set directories.user      /Tools/TC1004B/user
arduino-cli config set build_cache.path      /Tools/TC1004B/cache

# Refresh BOTH indexes (the zephyr core depends on libraries such as Arduino_RouterBridge)
arduino-cli core update-index
arduino-cli lib update-index

# Install the Zephyr core (with its compilers and tools) into /Tools/TC1004B/data
arduino-cli core install arduino:zephyr
```

### 6. Verify

```bash
arduino-flasher-cli --help   # the flasher runs from /Tools/TC1004B/flasher
arduino-cli config dump      # every path should start with /Tools/TC1004B
arduino-cli core list        # arduino:zephyr is listed
ls /Tools/TC1004B/data/packages/arduino   # hardware/ and tools/ for the Zephyr core
arduino-cli board list       # with the UNO Q plugged in and booted: a /dev/cu.usbmodem… port
```

---

## Part 2: Windows 10/11

All commands run in a normal (non-Administrator) **PowerShell** window, except the one-time flasher driver install in step 3, which needs Administrator. Creating folders directly under `C:\` is allowed for standard users on default Windows installs.

### 1. Create `C:\Tools\TC1004B`

```powershell
New-Item -ItemType Directory -Force C:\Tools\TC1004B | Out-Null
```

### 2. Set `PATH` and the Config File Location

Add both tool locations to your user `PATH` once, so both tools work from any folder after they're installed:

```powershell
# Add C:\Tools\TC1004B and C:\Tools\TC1004B\flasher to the user PATH (permanently) and to this session
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
foreach ($p in 'C:\Tools\TC1004B\flasher', 'C:\Tools\TC1004B') {
    if ($userPath -notlike "*$p*") { $userPath = "$p;$userPath" }
}
[Environment]::SetEnvironmentVariable('Path', $userPath, 'User')
$env:Path = "C:\Tools\TC1004B;C:\Tools\TC1004B\flasher;$env:Path"

# Default config file location for arduino-cli (permanently and for this session)
[Environment]::SetEnvironmentVariable('ARDUINO_CONFIG_FILE', 'C:\Tools\TC1004B\arduino-cli.yaml', 'User')
$env:ARDUINO_CONFIG_FILE = 'C:\Tools\TC1004B\arduino-cli.yaml'
```

### 3. Install `arduino-flasher-cli` into `C:\Tools\TC1004B\flasher`

The flasher is published for Windows only as **`windows-amd64`**, and its embedded `qdl` tool is x64-only. On Windows on ARM (Snapdragon PCs), it runs under x64 emulation.

1. Download the latest release and place `arduino-flasher-cli.exe` in `C:\Tools\TC1004B\flasher`. Run the same block again any time to **update** it.
   ```powershell
   New-Item -ItemType Directory -Force C:\Tools\TC1004B\flasher | Out-Null

   $rel   = Invoke-RestMethod https://api.github.com/repos/arduino/arduino-flasher-cli/releases/latest
   $asset = $rel.assets | Where-Object name -match 'windows-amd64.*\.(zip|tar\.gz)$' | Select-Object -First 1
   "Installing arduino-flasher-cli $($rel.tag_name) from $($asset.name)"

   # Extract into a temporary folder, then keep only the exe
   $tmp  = Join-Path $env:TEMP ("arduino-flasher-" + [guid]::NewGuid())
   New-Item -ItemType Directory $tmp | Out-Null
   $file = Join-Path $tmp $asset.name
   Invoke-WebRequest $asset.browser_download_url -OutFile $file
   if ($asset.name -like '*.zip') { Expand-Archive $file -DestinationPath $tmp -Force } else { tar -xzf $file -C $tmp }
   Get-ChildItem $tmp -Recurse -Filter arduino-flasher-cli.exe | Select-Object -First 1 |
       Move-Item -Destination C:\Tools\TC1004B\flasher -Force
   Remove-Item $tmp -Recurse -Force

   Unblock-File C:\Tools\TC1004B\flasher\arduino-flasher-cli.exe
   ```
2. **Install the EDL USB driver (one time, Administrator).** Open **PowerShell as Administrator** and run the flasher once:
   ```powershell
   C:\Tools\TC1004B\flasher\arduino-flasher-cli.exe
   ```
   On first run, it opens a prompt to install the USB driver for the board's Qualcomm EDL mode (`05C6:9008`). Since v0.5.3, it installs this driver with Windows' built-in `pnputil`. Accept it, then run the command again. Usage text with no prompt means the driver is in place. Close the Administrator window afterwards.
3. Back in a normal PowerShell window, verify and list the available OS image versions:
   ```powershell
   (Get-Command arduino-flasher-cli).Source     # C:\Tools\TC1004B\flasher\arduino-flasher-cli.exe
   arduino-flasher-cli list
   ```
4. **Flash a local OS image onto the board.** This writes a Debian OS image that's already on your PC to the UNO Q's internal eMMC storage. The image is a `.tar.zst` archive, for example one you downloaded earlier or a configured image made with Part 7 of the WSL guide. **Flashing erases everything on the board.** Use a normal (non-Administrator) PowerShell window.
   1. Copy the image into the flasher folder (adjust the source path to wherever your image is). The flasher also extracts the image while flashing, so keep several GB free on `C:`.
      ```powershell
      Copy-Item "$env:USERPROFILE\Downloads\arduino-unoq-debian-image-<VERSION>.tar.zst" C:\Tools\TC1004B\flasher\
      Get-ChildItem C:\Tools\TC1004B\flasher\*.tar.zst
      ```
   2. Put the board into **EDL (Emergency Download) mode**. Unplug the USB-C cable, short **JCTL pin 9 (`USB_BOOT`) to pin 10 (`GND`)** with a jumper wire, then plug the cable back in with the jumper still in place.

      ![Arduino UNO Q EDL Jumper Location](C:/Users/nxky0/.gemini/antigravity/brain/d80e0346-a00c-4d23-b268-4ba7e521cb63/uno_q_edl_jumper.png)
   3. Confirm Windows sees the board as a Qualcomm EDL device and that its driver loaded (`Status` should be `OK`):
      ```powershell
      Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_05C6&PID_9008' |
          Format-List FriendlyName, Class, Status, InstanceId
      ```
   4. Flash the local image. This takes several minutes, so don't unplug the board:
      ```powershell
      arduino-flasher-cli flash unoq C:\Tools\TC1004B\flasher\arduino-unoq-debian-image-<VERSION>.tar.zst --root-size 20GB
      # Example:
      # arduino-flasher-cli flash unoq C:\Tools\TC1004B\flasher\arduino-unoq-debian-image-20260528-558.tar.zst --root-size 20GB
      ```
      `--root-size` must be at least `10GB` and must fit your board. `20GB` matches the 32 GB UNO Q used in the WSL guide, so on a 16 GB board use a smaller value or omit the flag.
   5. When the flasher reports success, unplug the board, **remove the jumper**, and plug it back in to boot the new OS.

   > [!WARNING]
   > * **Skip the WSL-only steps:** no `usbipd` attach, no `qcserial` blacklist, no `sudo` and, above all, **no `ZlpAwareHost` `sed` patch**. That patch works around a `usbipd-win` bug and targets the Linux binary, so applying it to the Windows binary would corrupt it.
   > * **After a failed attempt, always power-cycle the board** (unplug and replug with the jumper in place) before retrying.
   > * **`unable to open USB device` / `Waiting for EDL device`:** This is the Windows counterpart of the WSL `qcserial` problem. Another driver, such as a Qualcomm *serial* "QDLoader 9008" COM-port driver from Windows Update or phone tools, may have claimed `05C6:9008`. In **Device Manager**, open the device, choose **Uninstall device**, tick **Attempt to remove the driver for this device**, unplug the board, re-run step 2 above as Administrator, and replug.
   > * **Antivirus:** Some antivirus products (for example, Norton) have flagged the flasher's bundled `qdl` tool and blocked flashing with `error flashing the board: exit status 1`. If this happens, add an exclusion for `C:\Tools\TC1004B\flasher`.

### 4. Install `arduino-cli` into `C:\Tools\TC1004B`

This downloads the latest official 64-bit `.zip` and extracts `arduino-cli.exe` into `C:\Tools\TC1004B`. Run the same block again any time to **update** it.

```powershell
Set-Location C:\Tools\TC1004B
$rel   = Invoke-RestMethod https://api.github.com/repos/arduino/arduino-cli/releases/latest
$asset = $rel.assets | Where-Object name -match 'Windows_64bit\.zip$' | Select-Object -First 1
"Installing arduino-cli $($rel.tag_name) from $($asset.name)"
Invoke-WebRequest $asset.browser_download_url -OutFile "$env:TEMP\$($asset.name)"
Expand-Archive "$env:TEMP\$($asset.name)" -DestinationPath C:\Tools\TC1004B -Force
Remove-Item "$env:TEMP\$($asset.name)"

# If the archive ever contains a sub-folder, move the exe up to the tools folder
$exe = Get-ChildItem C:\Tools\TC1004B -Recurse -Filter arduino-cli.exe | Select-Object -First 1
if ($exe.DirectoryName -ne 'C:\Tools\TC1004B') { Move-Item $exe.FullName C:\Tools\TC1004B -Force }
Unblock-File C:\Tools\TC1004B\arduino-cli.exe

(Get-Command arduino-cli).Source     # C:\Tools\TC1004B\arduino-cli.exe
arduino-cli version
```

> [!IMPORTANT]
> If you installed `arduino-cli` with winget earlier, remove it so there's only one copy on the `PATH`: `winget uninstall --exact --id ArduinoSA.CLI`.

### 5. Configure `arduino-cli` & Install the Zephyr Core

```powershell
New-Item -ItemType Directory -Force C:\Tools\TC1004B\data, C:\Tools\TC1004B\downloads, C:\Tools\TC1004B\user, C:\Tools\TC1004B\cache | Out-Null

arduino-cli config init --dest-dir C:\Tools\TC1004B --overwrite
arduino-cli config set directories.data      C:\Tools\TC1004B\data
arduino-cli config set directories.downloads C:\Tools\TC1004B\downloads
arduino-cli config set directories.user      C:\Tools\TC1004B\user
arduino-cli config set build_cache.path      C:\Tools\TC1004B\cache

# Refresh BOTH indexes, then install the Zephyr core (with its compilers and tools)
arduino-cli core update-index
arduino-cli lib update-index
arduino-cli core install arduino:zephyr
```

> [!TIP]
> The short root `C:\Tools\TC1004B` helps with the Zephyr core's deeply nested files and keeps you well clear of Windows' 260-character path limit.

### 6. Verify

```powershell
arduino-flasher-cli --help   # the flasher runs from C:\Tools\TC1004B\flasher
arduino-cli config dump      # every path should start with C:\Tools\TC1004B
arduino-cli core list        # arduino:zephyr is listed
Get-ChildItem C:\Tools\TC1004B\data\packages\arduino   # hardware\ and tools\ for the Zephyr core
arduino-cli board list       # with the UNO Q plugged in and booted: a COMx port
```

---

## Part 3: Compile a Test Sketch

The compile commands are the same as in Part 4 of the WSL guide. Only creating the sketch file differs by shell.

### 1. Create the Sketch

**macOS (zsh):**
```bash
mkdir -p led
cat << '__EOF__' > led/led.ino
#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
}
__EOF__
```

**Windows (PowerShell):**
```powershell
New-Item -ItemType Directory -Force led | Out-Null
@'
#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
}
'@ | Set-Content -Encoding ascii led\led.ino
```

### 2. Compile

Both commands work unchanged on macOS and Windows:

```bash
# Dynamic link mode (default)  ->  led/build/led.ino.elf-zsk.bin
arduino-cli compile -b arduino:zephyr:unoq --output-dir ./led/build ./led

# Static link mode             ->  led/build-static/led.ino.bin-zsk.bin
arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --output-dir ./led/build-static ./led
```

As in the WSL guide, use the `.elf-zsk.bin` file for Dynamic builds and the `.bin-zsk.bin` file for Static builds. Keep the two modes in separate output folders, or clean the folder when you switch, so you never pick up a stale payload. A successful build ends with a flash/RAM usage report against the UNO Q's limits (768 KB flash, 256 KB RAM).

---

## Part 4: Upload the Compiled Sketch via SSH

This follows **Part 5 (The Manual Upload)** of the WSL guide. Only the sketch payload you compiled in Part 3 is copied to the board's Linux side with `scp`. Then the board's own OpenOCD flashes it onto the STM32 microcontroller over `ssh`. Everything after the `ssh`/`scp` connection runs on the board, so the board-side commands are identical to the WSL guide.

**Assumption:** The board runs an OS image that already has SSH enabled and Ethernet-over-USB (CDC-NCM) configured, with the board at **`192.168.7.1`**, as set up in Part 2 of the WSL guide. The board is booted normally, with the EDL jumper removed.

> [!NOTE]
> Natively, there's nothing to bridge: macOS and Windows both have built-in drivers for the board's USB network adapter, so it gets an IP address automatically. The `usbipd` / WSL USB Manager "detach" step from the WSL guide doesn't apply. Both systems also include the OpenSSH client (`ssh`, `scp`).

> [!NOTE]
> As in the WSL guide, this method assumes the **Zephyr loader** already on the microcontroller is compatible with your installed `arduino:zephyr` core. It never re-flashes the loader. If you update the core and sketches stop running, do one official upload (or burn the bootloader) to refresh it.

### 1. Check the Connection

**macOS:**
```bash
ping -c 3 192.168.7.1
ssh arduino@192.168.7.1 "uname -a"
```

**Windows (PowerShell):**
```powershell
ping -n 3 192.168.7.1
ssh arduino@192.168.7.1 "uname -a"
```

On the first connection, type `yes` to accept the board's host key, then enter the `arduino` user's password you set on the board.

> [!TIP]
> * **No reply from `192.168.7.1`:** Check that your computer received an address on the board's network (the board hands out `192.168.7.x` by DHCP). On macOS, run `ifconfig | grep "inet 192.168.7."`. On Windows, run `Get-NetIPAddress | Where-Object IPAddress -like '192.168.7.*'`. If nothing shows, replug the USB-C cable and wait for the board to finish booting.
> * **`WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!`:** This is expected after you reflash the board, because the new image has new SSH host keys. Remove the old key with `ssh-keygen -R 192.168.7.1` (same command on macOS and Windows) and connect again.

### 2. (Optional) Set Up Passwordless SSH

This saves typing the password for every `ssh`/`scp` call, and it's needed later if you automate uploads.

**macOS:**
```bash
[ -f ~/.ssh/id_ed25519 ] || ssh-keygen -t ed25519      # create a key if you don't have one
ssh-copy-id arduino@192.168.7.1
```

**Windows (PowerShell):** Windows has no `ssh-copy-id`, so append the key on the board directly.
```powershell
if (-not (Test-Path $env:USERPROFILE\.ssh\id_ed25519)) { ssh-keygen -t ed25519 }   # create a key if you don't have one
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub |
    ssh arduino@192.168.7.1 "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
```

### 3. Upload the Sketch

Pick the payload that matches the link mode you compiled with in Part 3:

| Link mode | File to upload |
|---|---|
| Dynamic (default) | `led/build/led.ino.elf-zsk.bin` |
| Static | `led/build-static/led.ino.bin-zsk.bin` |

Run the commands from the folder that contains `led/`.

**macOS:**
```bash
export BOARD_HOST="arduino@192.168.7.1"

# 1. Create a staging folder on the board
ssh ${BOARD_HOST} "mkdir -p /tmp/remoteocd"

# 2. Copy the sketch payload
scp ./led/build/led.ino.elf-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin                # Dynamic
# scp ./led/build-static/led.ino.bin-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin       # Static

# 3. Flash the STM32 with the board's OpenOCD
ssh ${BOARD_HOST} "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'shutdown'"
```

**Windows (PowerShell):**
```powershell
$BOARD_HOST = "arduino@192.168.7.1"

# 1. Create a staging folder on the board
ssh $BOARD_HOST "mkdir -p /tmp/remoteocd"

# 2. Copy the sketch payload
scp .\led\build\led.ino.elf-zsk.bin "${BOARD_HOST}:/tmp/remoteocd/sketch.bin"                # Dynamic
# scp .\led\build-static\led.ino.bin-zsk.bin "${BOARD_HOST}:/tmp/remoteocd/sketch.bin"       # Static

# 3. Flash the STM32 with the board's OpenOCD
ssh $BOARD_HOST "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'shutdown'"
```

> [!NOTE]
> In PowerShell, write `"${BOARD_HOST}:/tmp/..."` with braces and double quotes. Without the braces, PowerShell would read `$BOARD_HOST:` as a drive-qualified variable name. The OpenOCD command is the same string as on macOS, because PowerShell passes the single quotes inside the double-quoted string through to the board unchanged.

When OpenOCD finishes, the STM32 restarts and runs your sketch. For the test sketch, the built-in LED blinks rapidly.

### 4. What the Flash Command Does

The command runs **on the board's Linux processor** and programs the STM32 over its internal SWD lines. It follows the sketch-flashing steps of the core's official `flash_sketch.cfg`:

| Option | Purpose |
|---|---|
| `-d2` | Info-level output: success/failure messages without verbose debug logs |
| `-s …` | Adds OpenOCD's script/config search paths on the board |
| `-f openocd_gpiod.cfg` | Board-specific interface: the Linux processor bit-bangs SWD to the STM32 through its GPIOs |
| `init` / `reset halt` | Connects over SWD, then resets and halts the STM32 so flash can be written safely |
| `flash write_image erase … 0x8100000 bin` | Erases and writes the payload at `0x08100000` (second flash bank, the core's sketch address). The Zephyr loader below this address is untouched. |
| `reset` / `sleep 100` | Restarts the STM32 on the new flash contents and waits 100 ms |
| `mww 0x40036400 0xCAFFEEEE` | Writes `0xCAFFEEEE` to `0x40036400`, exactly as the official script does after flashing a sketch. That it signals the loader to start the new sketch is an inference, not verified against the loader source. |
| `shutdown` | Closes OpenOCD and returns to your terminal |

> [!TIP]
> If you ever see unreliable connects or resets, add `-c 'reset_config srst_only srst_nogate srst_push_pull connect_assert_srst'` before `-c 'init'`. That matches the official `flash_sketch.cfg` more closely, as described in the WSL guide.

---

## Part 5: VSCode Compile Tasks

Both versions call the CLI by its full path inside `TC1004B`, just as the WSL guide uses `/opt/arduino-uno-q-tools/arduino-cli`. That way, VSCode always runs this copy, whatever its `PATH` looks like.

### macOS (`.vscode/tasks.json`)

The WSL guide's compile task with the path swapped:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Dynamic)",
            "type": "shell",
            "command": "/Tools/TC1004B/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\"",
            "options": { "env": { "ARDUINO_CONFIG_FILE": "/Tools/TC1004B/arduino-cli.yaml" } },
            "group": { "kind": "build", "isDefault": true },
            "presentation": { "reveal": "always", "panel": "new" },
            "problemMatcher": []
        },
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Static)",
            "type": "shell",
            "command": "/Tools/TC1004B/arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --build-path \"${fileDirname}/build-static/tmp\" --output-dir \"${fileDirname}/build-static\" \"${fileDirname}\"",
            "options": { "env": { "ARDUINO_CONFIG_FILE": "/Tools/TC1004B/arduino-cli.yaml" } },
            "presentation": { "reveal": "always", "panel": "new" },
            "problemMatcher": []
        }
    ]
}
```

### Windows (`.vscode/tasks.json`)

The `process` task type calls `arduino-cli.exe` directly, which avoids PowerShell quoting issues:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Dynamic)",
            "type": "process",
            "command": "C:\\Tools\\TC1004B\\arduino-cli.exe",
            "args": ["compile", "-b", "arduino:zephyr:unoq",
                     "--build-path", "${fileDirname}\\build\\tmp",
                     "--output-dir", "${fileDirname}\\build",
                     "${fileDirname}"],
            "options": { "env": { "ARDUINO_CONFIG_FILE": "C:\\Tools\\TC1004B\\arduino-cli.yaml" } },
            "group": { "kind": "build", "isDefault": true },
            "presentation": { "reveal": "always", "panel": "new" },
            "problemMatcher": []
        },
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Static)",
            "type": "process",
            "command": "C:\\Tools\\TC1004B\\arduino-cli.exe",
            "args": ["compile", "-b", "arduino:zephyr:unoq:link_mode=static",
                     "--build-path", "${fileDirname}\\build-static\\tmp",
                     "--output-dir", "${fileDirname}\\build-static",
                     "${fileDirname}"],
            "options": { "env": { "ARDUINO_CONFIG_FILE": "C:\\Tools\\TC1004B\\arduino-cli.yaml" } },
            "presentation": { "reveal": "always", "panel": "new" },
            "problemMatcher": []
        }
    ]
}
```

Open a `.ino` file and press **`Ctrl + Shift + B`** to compile in Dynamic mode. Run the Static task from **Tasks: Run Task**. The `ARDUINO_CONFIG_FILE` entry in each task makes VSCode use the `TC1004B` configuration even if it was started before you set the environment variable.

---

## Keeping Things Up to Date

| | macOS | Windows |
|---|---|---|
| Update the flasher | Re-run the download block from Part 1, step 3 | Re-run the download block from Part 2, step 3 |
| Update the CLI | Re-run the `install.sh` command from Part 1, step 4 | Re-run the download block from Part 2, step 4 |
| Update the Zephyr core | `arduino-cli core update-index && arduino-cli core upgrade` | same |
| Update libraries | `arduino-cli lib update-index && arduino-cli lib upgrade` | same |
| Clear the build cache | `rm -rf /Tools/TC1004B/cache/*` | `Remove-Item C:\Tools\TC1004B\cache\* -Recurse -Force` |

Updating either tool only replaces its program file. Local OS images, your configuration, cores and libraries in `TC1004B` are left as they are. Old OS images in `flasher/` can be deleted by hand once you no longer need them.

---

## Quick Reference

| | macOS | Windows |
|---|---|---|
| Tools folder | `/Tools/TC1004B` (via `/etc/synthetic.conf`) | `C:\Tools\TC1004B` |
| Flasher program | `/Tools/TC1004B/flasher/arduino-flasher-cli` | `C:\Tools\TC1004B\flasher\arduino-flasher-cli.exe` |
| OS images | `/Tools/TC1004B/flasher/*.tar.zst` | `C:\Tools\TC1004B\flasher\*.tar.zst` |
| Flash a local image (EDL jumper on) | `arduino-flasher-cli flash unoq /Tools/TC1004B/flasher/<image>.tar.zst --root-size 20GB` | `arduino-flasher-cli flash unoq C:\Tools\TC1004B\flasher\<image>.tar.zst --root-size 20GB` |
| CLI program | `/Tools/TC1004B/arduino-cli` | `C:\Tools\TC1004B\arduino-cli.exe` |
| Config file | `/Tools/TC1004B/arduino-cli.yaml` | `C:\Tools\TC1004B\arduino-cli.yaml` |
| `PATH` / `ARDUINO_CONFIG_FILE` set in | `~/.zshrc` | User environment variables |
| Core | `arduino-cli core install arduino:zephyr` | same |
| FQBN (Dynamic / Static) | `arduino:zephyr:unoq` / `arduino:zephyr:unoq:link_mode=static` | same |
| Serial port (normal boot) | `/dev/cu.usbmodem…` | `COMx` |
| Board address (SSH) | `arduino@192.168.7.1` | `arduino@192.168.7.1` |
| Upload payload (Dynamic) | `scp ./led/build/led.ino.elf-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin`, then the OpenOCD `ssh` command (Part 4) | `scp .\led\build\led.ino.elf-zsk.bin "arduino@192.168.7.1:/tmp/remoteocd/sketch.bin"`, then the OpenOCD `ssh` command (Part 4) |

---

## Sources

* [`arduino-flasher-cli` releases](https://github.com/arduino/arduino-flasher-cli/releases) · [v0.5.3 release notes](https://github.com/arduino/arduino-flasher-cli/releases/tag/v0.5.3) (`pnputil` driver install on Windows, `--root-size`)
* [Arduino docs — Flashing a new image to the UNO Q (PR #2661)](https://github.com/arduino/docs-content/pull/2661/files) (per-OS first run, Windows driver prompt)
* [`arduino-flasher-cli` issue #20](https://github.com/arduino/arduino-flasher-cli/issues/20) (antivirus flagging `qdl`, macOS 12 minimum)
* [Arduino Forum — Windows ARM64 build request](https://forum.arduino.cc/t/request-windows-arm64-build-of-arduino-flasher-cli-for-uno-q-qualcomm-windows-pcs/1431089) (amd64-only Windows builds)
* [Arduino CLI — Installation](https://docs.arduino.cc/arduino-cli/installation) (install script with `BINDIR`, official macOS/Windows downloads)
* [Homebrew: `arduino-cli`](https://formulae.brew.sh/formula/arduino-cli)
* [winget: `ArduinoSA.CLI`](https://www.wingetly.io/apps/arduino-sa/cli) (MSI, machine-wide install)
