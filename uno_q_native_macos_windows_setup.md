# Arduino UNO Q: Native `arduino-flasher-cli` & `arduino-cli` Setup on macOS & Windows

This guide adapts **Parts 1, 5, 6 and 7** of the [WSL & USBIPD Upload Guide](uno_q_wsl_upload_guide.md) so the OS image flasher (`arduino-flasher-cli`) and `arduino-cli` run **directly on macOS and Windows**, with no WSL. It covers installing the flasher and flashing an image (Part 1, section A), installing and configuring `arduino-cli` (Part 1, section B), compiling (Part 5), uploading a sketch over SSH (Part 6) and VSCode integration (Part 7).

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
├── qdl/                   # Windows only: Qualcomm Device Loader package (Part 0); macOS gets qdl from Homebrew
├── arduino-cli            # the CLI program (arduino-cli.exe on Windows)
├── arduino-cli.yaml       # configuration file (ARDUINO_CONFIG_FILE points here)
├── data/                  # cores + toolchains (data/packages/arduino/...): Zephyr core, compilers, tools
├── downloads/             # downloaded core/library archives
├── user/                  # sketchbook + installed libraries (user/libraries)
└── cache/                 # build cache
```

Your sketches live in a separate workspace folder, as in the WSL guide: `/workspace/arduino-uno-q` on macOS and `C:\workspace\arduino-uno-q` on Windows (Part 3).

---

## What Changes Compared to WSL

| | WSL (Ubuntu) | macOS (native) | Windows (native) |
|---|---|---|---|
| Tools folder | `/opt/arduino-uno-q-tools` | `/Tools/TC1004B` | `C:\Tools\TC1004B` |
| Flasher folder | `/opt/arduino-uno-q-tools/flasher` | `/Tools/TC1004B/flasher` | `C:\Tools\TC1004B\flasher` |
| Flasher download | `linux-amd64.tar.gz` | `darwin-arm64` (Apple Silicon) / `darwin-amd64` (Intel, if published) `.tar.gz` | `windows-amd64` archive |
| Flasher extras | `qcserial` blacklist + `ZlpAwareHost` patch + `usbipd` | `qdl` + `libusb` via Homebrew (Part 0). No patch, no driver blacklist. | Qualcomm Device Loader driver (Part 0), plus the flasher's first-run driver prompt (Administrator) |
| `arduino-cli` install method | `install.sh` with `BINDIR` | `install.sh` with `BINDIR` (same) | Official `.zip` from GitHub releases |
| `PATH` + `ARDUINO_CONFIG_FILE` set in | `~/.bashrc` | `~/.zshrc` | User environment variables |
| Creating a top-level folder | `sudo mkdir` | Needs `/etc/synthetic.conf` + reboot (see below) | `New-Item` |
| UNO Q serial port (normal boot) | `/dev/ttyACM0` | `/dev/cu.usbmodem…` | `COMx` |
| USB passthrough | `usbipd-win` required | Not needed | Not needed |

The board's USB ID in normal operation is `2341:0078`. Compiling needs no board connection at all.

---

## Part 0: Install QDL (Qualcomm Device Loader)

**QDL** is Qualcomm's open-source tool for talking to Qualcomm processors in **EDL (Emergency Download) mode**, which is how the UNO Q's Linux side is flashed. `arduino-flasher-cli` carries its own embedded copy of `qdl`, but your computer still needs the USB layer underneath it:

- **macOS:** the `libusb` library (and the standalone `qdl` tool), installed with Homebrew
- **Windows:** a USB driver for the board's EDL device (`05C6:9008`), from Qualcomm's **Qualcomm Device Loader** package

Install QDL before Part 1 (macOS) or Part 2 (Windows).

### macOS (Homebrew)

```bash
brew update
brew install qdl libusb pkg-config
```

| Package | Why |
|---|---|
| `qdl` | The standalone QDL tool ([linux-msm/qdl](https://github.com/linux-msm/qdl)) for flashing via EDL mode |
| `libusb` | The USB library `qdl` uses to reach the board. It's already a `qdl` dependency, and listing it makes sure it's installed and linked. |
| `pkg-config` | Lets build tools find `libusb`. Homebrew ships ready-made `qdl` builds only for Apple Silicon on recent macOS versions, so other Macs build `qdl` from source. Homebrew may install this as `pkgconf`, which provides the same `pkg-config` command. |

Verify:

```bash
which qdl                              # $(brew --prefix)/bin/qdl
qdl --version || qdl --help            # older builds may not support --version
brew list --versions qdl libusb
```

> [!NOTE]
> * macOS needs **no driver** for EDL mode. `libusb` talks to the board directly.
> * Homebrew installs `qdl` in its own location (`$(brew --prefix)/bin`), not in `/Tools/TC1004B`, because Homebrew can't install a package into a custom folder. `arduino-flasher-cli` keeps using its embedded copy either way.

### Windows (Qualcomm Device Loader Driver)

On Windows, you only need the **driver** from Qualcomm's Qualcomm Device Loader package. Run all the commands below in one **PowerShell window opened as Administrator**, because installing a driver requires it.

1. **Find the download on Qualcomm's website.** Follow the procedure on the [Qualcomm Device Loader page](https://softwarecenter.qualcomm.com/catalog/item/Qualcomm_Device_Loader) only up to the download: choose **Windows**, the version you want and the **x64** package (use x64 even on Windows on ARM PCs). The download link has this form, shown here for version 2.9.1:

   ```
   https://softwarecenter.qualcomm.com/api/download/software/tools/Qualcomm_Device_Loader/Windows/2.9.1/QDL_2.9.1_Win_x64.zip
   ```

   The link comes from the Qualcomm website, so set the operating system, version and file name in the next command to match the options you picked there.

2. **Download the `.zip`** into the tools folder:
   ```powershell
   # Set these to match the options you picked on the Qualcomm website
   $QdlOs      = 'Windows'
   $QdlVersion = '2.9.1'
   $QdlZip     = "QDL_${QdlVersion}_Win_x64.zip"
   $QdlUrl     = "https://softwarecenter.qualcomm.com/api/download/software/tools/Qualcomm_Device_Loader/$QdlOs/$QdlVersion/$QdlZip"

   New-Item -ItemType Directory -Force C:\Tools\TC1004B\qdl | Out-Null
   $ProgressPreference = 'SilentlyContinue'      # much faster downloads in Windows PowerShell
   Invoke-WebRequest $QdlUrl -OutFile "C:\Tools\TC1004B\qdl\$QdlZip"
   Get-Item "C:\Tools\TC1004B\qdl\$QdlZip" | Select-Object Name, Length
   ```
   If the variables were set correctly, the file appears with its size in bytes. You don't need to sign in to Qualcomm for this download.

3. **Install only the driver**, using the `install_driver.bat` file inside the `.zip`:
   ```powershell
   $QdlDir = "C:\Tools\TC1004B\qdl\QDL_$QdlVersion"
   Expand-Archive "C:\Tools\TC1004B\qdl\$QdlZip" -DestinationPath $QdlDir -Force

   $bat = Get-ChildItem $QdlDir -Recurse -Filter install_driver.bat | Select-Object -First 1
   Push-Location $bat.DirectoryName
   .\install_driver.bat
   Pop-Location
   ```
   `install_driver.bat` installs the Qualcomm EDL driver. Accept any Windows driver-installation prompts it shows.

To check the driver, put the board into EDL mode (EDL jumper on, then plug it in, as in Part 2, step 3). In **Device Manager**, it should appear as **Qualcomm HS-USB QDLoader 9008** under **libusb-win32 devices**. Exact names can vary with your Windows version.

> [!WARNING]
> **Driver conflict (`qdl: unable to open USB device`):** If the board appears as a "QDLoader 9008" entry under **Ports (COM & LPT)** instead, a third-party *serial* driver has claimed it, and `qdl` can't open it. Qualcomm's fix:
> 1. In **Device Manager**, right-click that entry and choose **Properties**. On the **General** tab, click **Change settings** and accept the elevation prompt.
> 2. On the **Driver** tab, click **Update Driver › Browse my computer for drivers › Let me pick from a list of available drivers on my computer**.
> 3. Select **Qualcomm HS-USB QDLoader 9008**, click **Next**, then **Close**.
> 4. Confirm the device now appears under **libusb-win32 devices**, then retry.

> [!NOTE]
> `arduino-flasher-cli` may still offer to install its own EDL driver on first run (Part 2, step 3). If it does and flashing then fails with `unable to open USB device`, check that the device is still listed as **Qualcomm HS-USB QDLoader 9008** (not under **Ports (COM & LPT)**), and switch it back with the steps above if needed.

---

## Part 1: macOS (Apple Silicon & Intel)

All commands run in **Terminal** (zsh, the macOS default shell).

### 1. Create `/Tools/TC1004B` and `/workspace/arduino-uno-q`

Since macOS Catalina, the root of the startup disk (`/`) is a **read-only system volume**, so `sudo mkdir /Tools` fails with `Read-only file system`. Apple's supported way to add a top-level folder is `/etc/synthetic.conf`. It makes a folder appear at `/` at boot as a link to a real folder on the writable Data volume.

This step sets up both top-level folders with a single restart: `/Tools` for the tools, and `/workspace` for your sketches (used in Part 3, as in Part 5 of the WSL guide).

1. Create the real folders on the Data volume. Make the tools folder yours, so nothing later needs `sudo`, and give the workspace global read/write permissions, as the WSL guide does:
   ```bash
   sudo mkdir -p /System/Volumes/Data/Tools/TC1004B
   sudo chown -R "$USER" /System/Volumes/Data/Tools
   sudo mkdir -p /System/Volumes/Data/workspace/arduino-uno-q
   sudo chmod -R a+w /System/Volumes/Data/workspace/arduino-uno-q
   ```
2. Register `/Tools` and `/workspace` in `/etc/synthetic.conf` (only once). The separator **must be a tab**, which `printf '\t'` guarantees:
   ```bash
   grep -qE '^Tools[[:space:]]' /etc/synthetic.conf 2>/dev/null || printf 'Tools\tSystem/Volumes/Data/Tools\n' | sudo tee -a /etc/synthetic.conf
   grep -qE '^workspace[[:space:]]' /etc/synthetic.conf 2>/dev/null || printf 'workspace\tSystem/Volumes/Data/workspace\n' | sudo tee -a /etc/synthetic.conf
   ```
3. **Restart the Mac.** `/Tools` and `/workspace` appear after the reboot.
4. Confirm:
   ```bash
   ls -ld /Tools /Tools/TC1004B /workspace /workspace/arduino-uno-q     # /Tools -> System/Volumes/Data/Tools, /workspace -> System/Volumes/Data/workspace
   touch /Tools/TC1004B/.write-test && rm /Tools/TC1004B/.write-test && echo "tools folder writable"
   touch /workspace/arduino-uno-q/.write-test && rm /workspace/arduino-uno-q/.write-test && echo "workspace writable"
   ```

> [!TIP]
> To undo this later, delete the `Tools` and `workspace` lines from `/etc/synthetic.conf`, restart, then remove `/System/Volumes/Data/Tools` and `/System/Volumes/Data/workspace`.

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
   FLASHER_URL=$(curl -fsSL https://api.github.com/repos/arduino/arduino-flasher-cli/releases/latest | grep -oE "https://[a-zA-Z0-9./_-]+darwin-${ARCH}\.tar\.gz" | head -1)
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
3. **Flash a local OS image onto the board.** This writes a Debian OS image that's already on your Mac to the UNO Q's internal eMMC storage. The image is a `.tar.zst` archive, for example one you downloaded earlier or a configured image made with Part 4 of the WSL guide. **Flashing erases everything on the board.**
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
foreach ($p in 'C:\Tools\TC1004B\flasher', 'C:\Tools\TC1004B') { if ($userPath -notlike "*$p*") { $userPath = "$p;$userPath" } }
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
   Get-ChildItem $tmp -Recurse -Filter arduino-flasher-cli.exe | Select-Object -First 1 | Move-Item -Destination C:\Tools\TC1004B\flasher -Force
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
4. **Flash a local OS image onto the board.** This writes a Debian OS image that's already on your PC to the UNO Q's internal eMMC storage. The image is a `.tar.zst` archive, for example one you downloaded earlier or a configured image made with Part 4 of the WSL guide. **Flashing erases everything on the board.** Use a normal (non-Administrator) PowerShell window.
   1. Copy the image into the flasher folder (adjust the source path to wherever your image is). The flasher also extracts the image while flashing, so keep several GB free on `C:`.
      ```powershell
      Copy-Item "$env:USERPROFILE\Downloads\arduino-unoq-debian-image-<VERSION>.tar.zst" C:\Tools\TC1004B\flasher\
      Get-ChildItem C:\Tools\TC1004B\flasher\*.tar.zst
      ```
   2. Put the board into **EDL (Emergency Download) mode**. Unplug the USB-C cable, short **JCTL pin 9 (`USB_BOOT`) to pin 10 (`GND`)** with a jumper wire, then plug the cable back in with the jumper still in place.

      ![Arduino UNO Q EDL Jumper Location](C:/Users/nxky0/.gemini/antigravity/brain/d80e0346-a00c-4d23-b268-4ba7e521cb63/uno_q_edl_jumper.png)
   3. Confirm Windows sees the board as a Qualcomm EDL device and that its driver loaded (`Status` should be `OK`):
      ```powershell
      Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_05C6&PID_9008' | Format-List FriendlyName, Class, Status, InstanceId
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

## Part 3: Compilation

This follows **Part 5 (Compilation)** of the WSL guide. The `arduino-cli` commands are identical. Only the workspace location and the shell commands for creating files differ.

### 1. Create the Workspace Folder

As in the WSL guide, sketches live in an `arduino-uno-q` workspace folder with global read/write permissions.

**macOS:** `/workspace/arduino-uno-q` was already created in Part 1, step 1, because macOS needs the `/etc/synthetic.conf` step and a restart for a top-level folder. Just go into it:
```bash
cd /workspace/arduino-uno-q
```

**Windows (PowerShell):** Create the folder, give everyone read/write access (the Windows equivalent of `chmod -R a+w`; `*S-1-1-0` is the built-in "Everyone" group in any Windows language), and go into it:
```powershell
New-Item -ItemType Directory -Force C:\workspace\arduino-uno-q | Out-Null
icacls C:\workspace\arduino-uno-q /grant "*S-1-1-0:(OI)(CI)M" /T
Set-Location C:\workspace\arduino-uno-q
```

### 2. Create the Sketch File

Arduino sketches require the main `.ino` file to be inside a folder of the exact same name, so create a `led` folder with `led.ino` inside it.

**macOS (zsh):**
```bash
mkdir -p led
printf '%s\n' '#include <Arduino.h>' '' 'void setup() {' '  pinMode(LED_BUILTIN, OUTPUT);' '}' '' 'void loop() {' '  digitalWrite(LED_BUILTIN, HIGH);' '  delay(50); // Simple software debounce' '  digitalWrite(LED_BUILTIN, LOW);' '  delay(50); // Simple software debounce' '}' > led/led.ino
```

**Windows (PowerShell):**
```powershell
New-Item -ItemType Directory -Force led | Out-Null
Set-Content -Encoding ascii led\led.ino -Value '#include <Arduino.h>', '', 'void setup() {', '  pinMode(LED_BUILTIN, OUTPUT);', '}', '', 'void loop() {', '  digitalWrite(LED_BUILTIN, HIGH);', '  delay(50); // Simple software debounce', '  digitalWrite(LED_BUILTIN, LOW);', '  delay(50); // Simple software debounce', '}'
```

### 3. Understanding Link Modes

When you compile a sketch for the UNO Q, `arduino-cli` does not produce a standalone firmware. The microcontroller always runs a pre-built **Zephyr loader** (the "bootloader" firmware shipped with the core), and your sketch is built as an add-on that this loader starts. *How* the sketch is joined to the loader is controlled by the board's **Link mode** option. For the full technical breakdown, see Appendix 3 of the WSL guide.

| | Dynamic (default) | Static |
|---|---|---|
| FQBN | `arduino:zephyr:unoq` | `arduino:zephyr:unoq:link_mode=static` |
| Final link | Relocatable (`ld -r`) | Fully linked at fixed addresses |
| Symbol file used | `syms-dynamic.ld` (exported API only) | `syms-static.ld` (all loader symbols) |
| File to upload | `<sketch>.ino.elf-zsk.bin` | `<sketch>.ino.bin-zsk.bin` |
| `zephyr-sketch-tool` flag | *(none)* | `-prelinked` |
| Work done at boot | Load into RAM, resolve symbols by name, relocate | None, starts the pre-linked image |
| Must the loader match exactly? | No, only its exported API | **Yes**, byte-for-byte |
| Hardware breakpoints | ❌ Fail (dynamic RAM address) | ✅ Work (fixed flash address) |

> [!TIP]
> **Which should I use?** Stay with **Dynamic** unless you have a specific reason not to, such as needing hardware breakpoint debugging with GDB. Its main weakness, a larger payload, rarely matters, and it keeps working when the loader on the board and your installed core drift slightly apart.

### 4. Compile the Sketch

From the workspace folder (the one containing `led/`), compile with one of the two link modes. These commands are the same on macOS and Windows.

#### Option A: Dynamic Mode (Default)

Dynamic is used when no link mode is specified. It produces a relocatable extension (`.elf-zsk.bin`) that the Zephyr loader links at boot time:
```bash
arduino-cli compile -b arduino:zephyr:unoq --output-dir ./led/build ./led
```
* **Upload file:** `./led/build/led.ino.elf-zsk.bin` (~74 KB)

#### Option B: Static Mode (Required for Hardware Breakpoints & Debugging)

Append `:link_mode=static` to the board FQBN. This resolves all symbols at build time to fixed flash addresses:
```bash
arduino-cli compile -b arduino:zephyr:unoq:link_mode=static --output-dir ./led/build ./led
```
* **Upload file:** `./led/build/led.ino.bin-zsk.bin` (~24 KB)

> [!IMPORTANT]
> **Upload the file that matches the mode you compiled with:**
> - **Dynamic:** upload `led.ino.elf-zsk.bin`.
> - **Static:** upload `led.ino.bin-zsk.bin`.
>
> **Both files are created in either mode**, so finding a file in `build/` doesn't make it valid. In a Dynamic build, the `.bin-zsk.bin` is just a raw dump of an unlinked object and **will not run**. In a Static build, the `.elf-zsk.bin` is not the upload file. If you switch between modes, clean the `build/` folder before recompiling to avoid flashing a stale payload:
> - **macOS:** `rm -rf ./led/build`
> - **Windows:** `Remove-Item -Recurse -Force .\led\build`

* **Why `--output-dir`?** By default, `arduino-cli` keeps compiled binaries in its build cache. With this guide's configuration, that's under `/Tools/TC1004B/cache` (`C:\Tools\TC1004B\cache` on Windows). This flag copies the output into `./led/build/`, so the correct upload file is easy to find.

A successful build ends with a flash/RAM usage report against the UNO Q's limits (768 KB flash, 256 KB RAM).

---

## Part 4: Upload the Compiled Sketch via SSH

This follows **Part 6 (The Manual Upload)** of the WSL guide. Instead of `arduino-cli upload`, the sketch payload you compiled in Part 3 is copied to the board's Linux side with `scp`, and the board's own OpenOCD flashes it onto the STM32 microcontroller over `ssh`. Everything after the `ssh`/`scp` connection runs on the board, so the board-side commands are identical to the WSL guide.

**Assumption:** The board runs an OS image that already has SSH enabled and Ethernet-over-USB (CDC-NCM) configured, with the board at **`192.168.7.1`** (or its mDNS hostname `uno-q.local`), as set up in Part 2 of the WSL guide. The board is booted normally, with the EDL jumper removed.

> [!NOTE]
> Natively, there's nothing to bridge: macOS and Windows both have built-in drivers for the board's USB network adapter, so it gets an IP address automatically. The `usbipd` / WSL USB Manager "detach" step from the WSL guide doesn't apply. Both systems also include the OpenSSH client (`ssh`, `scp`).

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
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub | ssh arduino@192.168.7.1 "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
```

### 3. Set the Board Address & Create the Staging Folder

Set `BOARD_HOST` to the board's Ethernet-over-USB IP (`192.168.7.1`) or mDNS hostname (`uno-q.local`), then create a temporary staging folder on the board's Linux side. Run all upload commands from your workspace folder (`/workspace/arduino-uno-q` or `C:\workspace\arduino-uno-q`).

**macOS:**
```bash
export BOARD_HOST="arduino@192.168.7.1"
ssh ${BOARD_HOST} "mkdir -p /tmp/remoteocd"
```

**Windows (PowerShell):**
```powershell
$BOARD_HOST = "arduino@192.168.7.1"
ssh $BOARD_HOST "mkdir -p /tmp/remoteocd"
```

### 4. One-Time Step: Update the STM32's Zephyr Loader

> [!IMPORTANT]
> **Do this once for a new board, and again after every `arduino:zephyr` core install or upgrade.**
> Flashing the Debian OS with `arduino-flasher-cli` only programs the Qualcomm eMMC. It does **not** update the STM32 microcontroller's internal flash (`0x08000000`), where the Zephyr loader lives. When `arduino-cli core install/upgrade arduino:zephyr` installs a newer core (such as `arduino:zephyr 1.1.0`, whose loader is 265,460 bytes vs. the factory 263,680-byte loader), sketches compiled with it import updated LLEXT symbols (`pinctrl_configure_pins`, `__real_*` wrappers and updated device ordinals) that the older factory loader doesn't export. **The sketch then fails to load, and nothing happens on the board (for example, the LED never blinks)** until the loader is updated once.

The commands below pick the loader `.bin` from the newest installed `arduino:zephyr` core, copy it to the board and write it to `0x08000000`.

**macOS:**
```bash
LOADER_BIN=$(ls /Tools/TC1004B/data/packages/arduino/hardware/zephyr/*/firmwares/zephyr-arduino_uno_q_stm32u585xx.bin | sort -V | tail -n 1)
echo "${LOADER_BIN}"
scp "${LOADER_BIN}" ${BOARD_HOST}:/tmp/remoteocd/loader.bin
ssh ${BOARD_HOST} "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/loader.bin 0x08000000 bin' -c 'reset halt' -c 'resume' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
```

**Windows (PowerShell):**
```powershell
$LOADER_BIN = (Get-ChildItem C:\Tools\TC1004B\data\packages\arduino\hardware\zephyr\*\firmwares\zephyr-arduino_uno_q_stm32u585xx.bin | Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1).FullName
$LOADER_BIN
scp $LOADER_BIN "${BOARD_HOST}:/tmp/remoteocd/loader.bin"
ssh $BOARD_HOST "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/loader.bin 0x08000000 bin' -c 'reset halt' -c 'resume' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
```

Once the STM32 has the matching Zephyr loader at `0x08000000`, every later upload only needs steps 5 and 6.

### 5. Copy the Sketch Payload via `scp`

Copy only your compiled sketch payload, matching the link mode you compiled with in Part 3:

| Link mode | File to upload |
|---|---|
| Dynamic (default) | `led/build/led.ino.elf-zsk.bin` |
| Static | `led/build/led.ino.bin-zsk.bin` |

**macOS:**
```bash
scp ./led/build/led.ino.elf-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin        # Dynamic (default)
# scp ./led/build/led.ino.bin-zsk.bin ${BOARD_HOST}:/tmp/remoteocd/sketch.bin      # Static
```

**Windows (PowerShell):**
```powershell
scp .\led\build\led.ino.elf-zsk.bin "${BOARD_HOST}:/tmp/remoteocd/sketch.bin"        # Dynamic (default)
# scp .\led\build\led.ino.bin-zsk.bin "${BOARD_HOST}:/tmp/remoteocd/sketch.bin"      # Static
```

> [!NOTE]
> **How big is the payload?** There is no 100 KB limit. For reference, an LED blink sketch produces a **~76 KB** `.elf-zsk.bin`. Sketches that pull in libraries (e.g., `Arduino_RouterBridge`) are larger. The real limits, defined by the core's `boards.txt`, are **768 KB (786,432 bytes) of flash** and **256 KB (262,144 bytes) of RAM**, and `arduino-cli compile` prints your usage against them after every build. Check the payload size with `ls -l ./led/build/*.elf-zsk.bin` (macOS) or `Get-Item .\led\build\*.elf-zsk.bin` (Windows).

### 6. Flash the Microcontroller via `ssh`

Run the board's internal OpenOCD over `ssh` to flash the STM32 directly:

**macOS:**
```bash
ssh ${BOARD_HOST} "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
```

**Windows (PowerShell):**
```powershell
ssh $BOARD_HOST "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
```

> [!NOTE]
> In PowerShell, write `"${BOARD_HOST}:/tmp/..."` with braces and double quotes. Without the braces, PowerShell would read `$BOARD_HOST:` as a drive-qualified variable name. The OpenOCD commands are the same strings as on macOS, because PowerShell passes the single quotes inside the double-quoted string through to the board unchanged.

When OpenOCD finishes, the STM32 restarts and runs your sketch. For the test sketch, the red channel of RGB LED 3 (`LED_BUILTIN`) blinks rapidly: 50 ms on, 50 ms off. If nothing happens, run the one-time loader update in step 4, then repeat steps 5 and 6.

### 7. Understanding the Flash Command

Because the UNO Q has two processors (a Linux processor and an STM32 microcontroller), the command runs **on the board's Linux processor** and programs the STM32 directly. The sequence mirrors the sketch-flashing steps of the core's official `flash_sketch.cfg` script:

| Option | Purpose |
|---|---|
| `/opt/openocd/bin/openocd` | The OpenOCD executable on the board's Linux filesystem |
| `-d2` | Info-level output: success/failure messages without verbose debug logs |
| `-s …` | Adds directories to OpenOCD's search path so it can find its scripts and configuration files |
| `-f openocd_gpiod.cfg` | Board-specific interface: the Linux processor uses its GPIO pins to "bitbang" the SWD (Serial Wire Debug) protocol to the STM32 |
| `init` | Initializes OpenOCD and connects to the STM32 over SWD |
| `reset halt` | Resets the STM32 and halts its CPU so flash can be written from a known, stable state |
| `flash write_image erase … 0x8100000 bin` | Erases and writes the sketch payload starting at `0x08100000`: the start of the STM32U585's second flash bank, which is the core's configured sketch address (`unoq.upload.address`). The Zephyr loader lives below this address and isn't touched. |
| `reset halt` / `resume` | Resets the STM32 and resumes execution from the updated flash, while preserving the firmware's DWT/DebugMonitor setup |
| `sleep 100` | Waits 100 ms so the Zephyr loader completes its initial clear of the backup SRAM mailbox (`0x40036400`) |
| `mww 0x40036400 0xCAFFEEEE` | "Memory Write Word": writes `0xCAFFEEEE` to backup SRAM at `0x40036400` after the delay, to signal the loader that a fresh sketch upload completed |
| `mww 0xE000EDF0 0xA05F0000` | Clears `DHCSR.C_DEBUGEN` (using the Cortex-M `DBGKEY` `0xA05F0000`), which OpenOCD leaves set, so DWT null-pointer traps raise `DebugMonitor` instead of halting the CPU when no debugger is attached |
| `shutdown` | Closes OpenOCD and returns to your terminal |

The one-time loader command in step 4 uses the same building blocks. It writes `loader.bin` to `0x08000000` (the start of flash) instead of `0x8100000`, then runs `reset halt` / `resume` and the `0xE000EDF0` write. It doesn't need the `sleep 100` / `0xCAFFEEEE` mailbox signal.

> [!NOTE]
> The official `flash_sketch.cfg` also sets `reset_config srst_only srst_nogate srst_push_pull connect_assert_srst` before `init`. The commands above don't set it explicitly. If you ever see unreliable connects or resets, adding `-c 'reset_config srst_only srst_nogate srst_push_pull connect_assert_srst'` before `-c 'init'` matches the official behaviour more closely.

---

## Part 5: VSCode Integration via SSH (Dynamic Link Mode & Unified Tasks)

This follows **Part 7** of the WSL guide. With the board reachable over standard IP, either via **Ethernet-over-USB** at `192.168.7.1` or via **Wi-Fi** at `uno-q.local`, you can compile and upload in one step from VSCode, using the default **Dynamic link mode**.

*(For hardware breakpoint debugging with GDB, which needs Static link mode, see Appendix 4 of the WSL guide.)*

> [!NOTE]
> **Keyboard shortcuts:** This part lists the Windows shortcuts. On macOS, use **`Cmd`** instead of **`Ctrl`** (for example, **`Cmd + Shift + B`** and **`Cmd + Shift + P`**).

### 1. Configure Passwordless SSH (One-Time Setup)

The tasks run `ssh` and `scp` without stopping for a password, so set up a key first. If you already did Part 4, step 2 for the address you use, you can skip this.

**macOS:**
```bash
[ -f ~/.ssh/id_ed25519 ] || ssh-keygen -t ed25519      # create a key if you don't have one
ssh-copy-id arduino@192.168.7.1                        # Ethernet-over-USB
# ssh-copy-id arduino@uno-q.local                      # Wi-Fi
```

**Windows (PowerShell):**
```powershell
if (-not (Test-Path $env:USERPROFILE\.ssh\id_ed25519)) { ssh-keygen -t ed25519 }   # create a key if you don't have one
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub | ssh arduino@192.168.7.1 "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"      # Ethernet-over-USB
# Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub | ssh arduino@uno-q.local "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"    # Wi-Fi
```

### 2. Open the Workspace Folder in VSCode

> [!NOTE]
> Natively, VSCode runs directly on macOS or Windows, so the **Remote Development / WSL extension from the WSL guide isn't needed**. Open the workspace folder from Part 3 as a normal local folder.

1. **Open the workspace folder:**
   * Go to **File › Open Folder…** and choose `/workspace/arduino-uno-q` (macOS) or `C:\workspace\arduino-uno-q` (Windows).
   * *(Alternatively, from a terminal, run `code /workspace/arduino-uno-q` on macOS or `code C:\workspace\arduino-uno-q` on Windows. On macOS, enable the `code` command once with `Cmd + Shift + P` › **Shell Command: Install 'code' command in PATH**.)*
2. **Turn off Restricted Mode (trust the workspace):**
   * When VSCode opens the folder, click **Yes, I trust the authors** in the Workspace Trust dialog.
   * If the folder opens in **Restricted Mode**, click the **Restricted Mode** indicator in the top banner or bottom status bar, select **Manage**, and click **Trust Folder**. *(VSCode doesn't run `.vscode/tasks.json` tasks while Restricted Mode is active.)*

### 3. Configure VSCode Tasks (`.vscode/tasks.json`)

Create or update `.vscode/tasks.json` in your workspace folder. Both versions below define the same two tasks:

* **Arduino UNO Q: Compile Current Sketch (Dynamic):** compiles the sketch that's open in the editor (`arduino:zephyr:unoq`).
* **Arduino UNO Q: Upload via SSH (Dynamic):** compiles, then uses `ssh` and `scp` to copy the `.elf-zsk.bin` payload to the board and flash it with the board's OpenOCD. The OpenOCD command is the one from Part 4, step 6.

Both call `arduino-cli` by its full path inside `TC1004B` (as the WSL guide uses `/opt/arduino-uno-q-tools/arduino-cli`) and set `ARDUINO_CONFIG_FILE`. That way, VSCode always uses this copy and its configuration, even if it was started before you set the environment variables.

#### macOS

This is the WSL guide's `tasks.json` with the `arduino-cli` path changed. VSCode runs it in your default shell (zsh), which handles the `&&` chaining and the `*.elf-zsk.bin` wildcard exactly as in WSL.

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Dynamic)",
            "type": "shell",
            "command": "/Tools/TC1004B/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\"",
            "options": {
                "env": {
                    "ARDUINO_CONFIG_FILE": "/Tools/TC1004B/arduino-cli.yaml"
                }
            },
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
            "command": "/Tools/TC1004B/arduino-cli compile -b arduino:zephyr:unoq --build-path \"${fileDirname}/build/tmp\" --output-dir \"${fileDirname}/build\" \"${fileDirname}\" && ssh arduino@192.168.7.1 \"mkdir -p /tmp/remoteocd\" && scp \"${fileDirname}/build/\"*.elf-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin && ssh arduino@192.168.7.1 \"/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'\"",
            "options": {
                "env": {
                    "ARDUINO_CONFIG_FILE": "/Tools/TC1004B/arduino-cli.yaml"
                }
            },
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

#### Windows

Windows PowerShell 5.1 (VSCode's default shell on Windows) doesn't support `&&`, and `scp` on Windows doesn't expand wildcards like `*.elf-zsk.bin`. So here, the upload task runs the same four steps (compile, create the staging folder, copy, flash) as a **sequence of tasks**, each calling its program directly with no shell quoting involved. If a step fails, the sequence stops there. The three helper steps are hidden (`"hide": true`), so **Tasks: Run Task** only lists the two main tasks.

The copy step names the payload explicitly as `${fileBasename}.elf-zsk.bin`, which for `led.ino` is `led.ino.elf-zsk.bin`.

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Arduino UNO Q: Compile Current Sketch (Dynamic)",
            "type": "process",
            "command": "C:\\Tools\\TC1004B\\arduino-cli.exe",
            "args": [
                "compile",
                "-b",
                "arduino:zephyr:unoq",
                "--build-path",
                "${fileDirname}\\build\\tmp",
                "--output-dir",
                "${fileDirname}\\build",
                "${fileDirname}"
            ],
            "options": {
                "env": {
                    "ARDUINO_CONFIG_FILE": "C:\\Tools\\TC1004B\\arduino-cli.yaml"
                }
            },
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "reveal": "always",
                "panel": "shared"
            },
            "problemMatcher": []
        },
        {
            "label": "UNO Q upload step 1: create staging folder",
            "hide": true,
            "type": "process",
            "command": "ssh",
            "args": [
                "arduino@192.168.7.1",
                "mkdir -p /tmp/remoteocd"
            ],
            "presentation": {
                "reveal": "always",
                "panel": "shared"
            },
            "problemMatcher": []
        },
        {
            "label": "UNO Q upload step 2: copy payload",
            "hide": true,
            "type": "process",
            "command": "scp",
            "args": [
                "${fileDirname}\\build\\${fileBasename}.elf-zsk.bin",
                "arduino@192.168.7.1:/tmp/remoteocd/sketch.bin"
            ],
            "presentation": {
                "reveal": "always",
                "panel": "shared"
            },
            "problemMatcher": []
        },
        {
            "label": "UNO Q upload step 3: flash STM32",
            "hide": true,
            "type": "process",
            "command": "ssh",
            "args": [
                "arduino@192.168.7.1",
                "/opt/openocd/bin/openocd -d2 -s /opt/openocd -s /opt/openocd/share/openocd/scripts -f openocd_gpiod.cfg -c 'init' -c 'reset halt' -c 'flash write_image erase /tmp/remoteocd/sketch.bin 0x8100000 bin' -c 'reset halt' -c 'resume' -c 'sleep 100' -c 'mww 0x40036400 0xCAFFEEEE' -c 'mww 0xE000EDF0 0xA05F0000' -c 'shutdown'"
            ],
            "presentation": {
                "reveal": "always",
                "panel": "shared"
            },
            "problemMatcher": []
        },
        {
            "label": "Arduino UNO Q: Upload via SSH (Dynamic)",
            "dependsOrder": "sequence",
            "dependsOn": [
                "Arduino UNO Q: Compile Current Sketch (Dynamic)",
                "UNO Q upload step 1: create staging folder",
                "UNO Q upload step 2: copy payload",
                "UNO Q upload step 3: flash STM32"
            ],
            "group": {
                "kind": "test",
                "isDefault": true
            },
            "problemMatcher": []
        }
    ]
}
```

*(Note: If you use Wi-Fi instead of Ethernet-over-USB, change every `arduino@192.168.7.1` to `arduino@uno-q.local`. There are two on macOS and three on Windows.)*

### 4. Running Build & Upload Tasks in VSCode

1. Open your `.ino` sketch file in VSCode (e.g., `led/led.ino`).
2. **To compile only:** Press **`Ctrl + Shift + B`** (macOS: **`Cmd + Shift + B`**). This runs the default build task, **Arduino UNO Q: Compile Current Sketch (Dynamic)**.
3. **To compile & upload:** Open the Command Palette (**`Ctrl + Shift + P`**, macOS: **`Cmd + Shift + P`**), select **Tasks: Run Task**, and choose **Arduino UNO Q: Upload via SSH (Dynamic)**.

> [!TIP]
> If the upload runs but the sketch doesn't start (for example, the LED never blinks), the STM32's Zephyr loader probably doesn't match your installed core. Run the one-time loader update in Part 4, step 4, then upload again.

---

## Keeping Things Up to Date

| | macOS | Windows |
|---|---|---|
| Update the flasher | Re-run the download block from Part 1, step 3 | Re-run the download block from Part 2, step 3 |
| Update the CLI | Re-run the `install.sh` command from Part 1, step 4 | Re-run the download block from Part 2, step 4 |
| Update the Zephyr core | `arduino-cli core update-index && arduino-cli core upgrade`, then redo the one-time loader update (Part 4, step 4) | same |
| Update libraries | `arduino-cli lib update-index && arduino-cli lib upgrade` | same |
| Clear the build cache | `rm -rf /Tools/TC1004B/cache/*` | `Remove-Item C:\Tools\TC1004B\cache\* -Recurse -Force` |

Updating either tool only replaces its program file. Local OS images, your configuration, cores and libraries in `TC1004B` are left as they are. Old OS images in `flasher/` can be deleted by hand once you no longer need them.

---

## Quick Reference

| | macOS | Windows |
|---|---|---|
| Tools folder | `/Tools/TC1004B` (via `/etc/synthetic.conf`) | `C:\Tools\TC1004B` |
| QDL (Part 0) | `brew install qdl libusb pkg-config` | `QDL_<version>_Win_x64.zip` in `C:\Tools\TC1004B\qdl`, then its `install_driver.bat` (Administrator) |
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
| Workspace (sketches) | `/workspace/arduino-uno-q` (via `/etc/synthetic.conf`) | `C:\workspace\arduino-uno-q` |
| One-time loader update | Part 4, step 4 (new board / after every core install or upgrade) | same |
| Upload payload (Dynamic) | `scp ./led/build/led.ino.elf-zsk.bin arduino@192.168.7.1:/tmp/remoteocd/sketch.bin`, then the OpenOCD `ssh` command (Part 4) | `scp .\led\build\led.ino.elf-zsk.bin "arduino@192.168.7.1:/tmp/remoteocd/sketch.bin"`, then the OpenOCD `ssh` command (Part 4) |

---

## Sources

* [Qualcomm Software Center: Qualcomm Device Loader](https://softwarecenter.qualcomm.com/catalog/item/Qualcomm_Device_Loader) (Windows driver package)
* [Qualcomm docs: USB driver conflict](https://dragonwingdocs.qualcomm.com/Tools/Qualcomm-Launcher/troubleshooting/usb-driver-conflict) (Device Manager names, fixing a QDLoader entry under Ports (COM & LPT))
* [Qualcomm docs: Flash with QDL](https://dragonwingdocs.qualcomm.com/Key-Documents/Flash-Guide/flash-with-qdl)
* [Homebrew: `qdl`](https://formulae.brew.sh/formula/qdl) · [linux-msm/qdl](https://github.com/linux-msm/qdl)

* [`arduino-flasher-cli` releases](https://github.com/arduino/arduino-flasher-cli/releases) · [v0.5.3 release notes](https://github.com/arduino/arduino-flasher-cli/releases/tag/v0.5.3) (`pnputil` driver install on Windows, `--root-size`)
* [Arduino docs — Flashing a new image to the UNO Q (PR #2661)](https://github.com/arduino/docs-content/pull/2661/files) (per-OS first run, Windows driver prompt)
* [`arduino-flasher-cli` issue #20](https://github.com/arduino/arduino-flasher-cli/issues/20) (antivirus flagging `qdl`, macOS 12 minimum)
* [Arduino Forum — Windows ARM64 build request](https://forum.arduino.cc/t/request-windows-arm64-build-of-arduino-flasher-cli-for-uno-q-qualcomm-windows-pcs/1431089) (amd64-only Windows builds)
* [Arduino CLI — Installation](https://docs.arduino.cc/arduino-cli/installation) (install script with `BINDIR`, official macOS/Windows downloads)
* [Homebrew: `arduino-cli`](https://formulae.brew.sh/formula/arduino-cli)
* [winget: `ArduinoSA.CLI`](https://www.wingetly.io/apps/arduino-sa/cli) (MSI, machine-wide install)
