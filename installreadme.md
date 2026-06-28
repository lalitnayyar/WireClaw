# WireClaw — Install & Flash (Windows / ESP32-C6)

Guide for building, flashing, and using WireClaw on ESP32-C6 boards — including the **Waveshare ESP32-C6-LCD-1.47** with onboard 1.47" display.

---

## Supported boards

| Board | PlatformIO env | Flash | Display |
|-------|------------------|-------|---------|
| **Waveshare ESP32-C6-LCD-1.47** | `esp32-c6-lcd` **(default)** | 8 MB | Onboard ST7789 172×320 LCD + RGB LED (GPIO8) |
| Espressif ESP32-C6-DevKitC-1 | `esp32-c6` | 4 MB | No LCD — use web Status tab only |

### Waveshare ESP32-C6-LCD-1.47 specs

- **MCU:** ESP32-C6 (160 MHz, Wi-Fi 6, BLE 5)
- **Display:** 1.47" TFT, **172×320**, 262K color, controller **ST7789** (SPI)
- **RGB LED:** WS2812-style on **GPIO8**
- **Wiki:** [Waveshare ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)

**LCD pin mapping (fixed on board):**

| LCD signal | GPIO |
|------------|------|
| MOSI | 6 |
| SCLK | 7 |
| CS | 14 |
| DC | 15 |
| RST | 21 |
| Backlight (BL) | 22 |

WireClaw includes a built-in ST7789 driver for this board (no TFT_eSPI — it does not compile on ESP32-C6 + Arduino 3.x).

---

## Quick start

```powershell
cd c:\Projects\wireclaw\WireClaw

# One-time (Windows + AVG)
.\scripts\setup-pio-ssl.ps1
.\scripts\bootstrap-platform.ps1

# Config (WiFi, API key, Telegram, etc.)
copy data\config.json.example data\config.json
# Edit data\config.json

# Flash firmware + LittleFS config (default env: esp32-c6-lcd)
python scripts\flash.py --port COM3
```

Replace `COM3` with your port (see below).

---

## Find your serial port

### PlatformIO (recommended)

```powershell
$env:Path = "$env:USERPROFILE\.platformio\penv\Scripts;" + $env:Path
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
pio device list
```

Or use the project wrapper:

```powershell
.\scripts\pio.ps1 device list
```

### PowerShell

```powershell
[System.IO.Ports.SerialPort]::getportnames()
```

### Device Manager

1. Win + X → **Device Manager**
2. Expand **Ports (COM & LPT)**
3. Look for **USB Serial Device (COMx)** or **USB JTAG/serial (COMx)**

### Python (pyserial)

```powershell
python -c "from serial.tools import list_ports; [print(p.device, p.description) for p in list_ports.comports()]"
```

---

## Configuration (`data/config.json`)

Copy and edit before flashing (filesystem upload puts this on the device):

```powershell
copy data\config.json.example data\config.json
```

Key fields:

| Field | Purpose |
|-------|---------|
| `wifi_ssid` / `wifi_pass` | WiFi credentials |
| `api_key` | OpenRouter (or compatible) API key |
| `model` | LLM model name (e.g. `google/gemini-2.5-flash`) |
| `device_name` | mDNS hostname (e.g. `wireclaw-01` → `http://wireclaw-01.local/`) |
| `telegram_token` / `telegram_chat_id` | Optional Telegram bot |
| `timezone` | POSIX TZ string (e.g. `UTC4`) |

Reboot required after changing config via web UI. Reflash filesystem or use web Save + Reboot for initial upload.

---

## Flash firmware + config

### Python script (recommended)

`scripts/flash.py` — auto-detects COM port, applies Windows SSL/UTF-8 fixes, shows step progress.

```powershell
# Waveshare LCD board (default env: esp32-c6-lcd)
python scripts\flash.py --port COM3

# Plain ESP32-C6 DevKit (no onboard LCD)
python scripts\flash.py --port COM3 -e esp32-c6

# Other options
python scripts\flash.py --port COM3 --monitor      # flash + serial monitor (115200)
python scripts\flash.py --port COM3 --firmware-only # skip LittleFS / config
python scripts\flash.py --port COM3 --fs-only       # config only
python scripts\flash.py -e esp32-s3 --port COM5     # different chip
```

**What gets flashed:**

1. **Firmware** — WireClaw application (PlatformIO `upload`)
2. **Filesystem** — `data/config.json` and optional `system_prompt.txt` (PlatformIO `uploadfs`)

### Flash progress output

```
============================================================
[1/2] Build & upload firmware
============================================================
> pio.exe run -e esp32-c6-lcd -v -t upload --upload-port COM3 ...
  >> Compiling .pio/build/esp32-c6-lcd/src/main.cpp.o
  >> Uploading .pio/build/esp32-c6-lcd/firmware.bin
  >> Writing at 0x00010000... (100 %)

============================================================
[2/2] Upload filesystem (config + prompt)
============================================================
...
============================================================
Flash complete.
============================================================
```

- Step headers: `[1/2]`, `[2/2]`
- Verbose PlatformIO output (`-v`)
- Progress lines prefixed with `>>`

First build can take several minutes (toolchain download). Later builds are much faster.

### Manual PlatformIO (via wrapper)

Always prefer `scripts\flash.py` or `scripts\pio.ps1` on Windows with AVG — they clear `SSLKEYLOGFILE` and set `UV_NATIVE_TLS=1`.

```powershell
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
$env:UV_NATIVE_TLS = "1"
$env:PYTHONUTF8 = "1"
$env:CI = "1"

# LCD board
.\scripts\pio.ps1 run -e esp32-c6-lcd -t upload --upload-port COM3
.\scripts\pio.ps1 run -e esp32-c6-lcd -t uploadfs --upload-port COM3
.\scripts\pio.ps1 device monitor -e esp32-c6-lcd --port COM3 -b 115200
```

### Project scripts

| Script | Purpose |
|--------|---------|
| `scripts/flash.py` | Build + upload firmware + filesystem; optional monitor |
| `scripts/pio.ps1` | PlatformIO wrapper (SSL, UTF-8, AVG fixes) |
| `scripts/setup-pio-ssl.ps1` | One-time: pip-system-certs, setuptools, wheel offline |
| `scripts/bootstrap-platform.ps1` | One-time: ESP32 platform install via curl |

---

## One-time setup (Windows + AVG antivirus)

AVG can break PlatformIO HTTPS:

| Symptom | Cause |
|---------|--------|
| `avgMonFltProxy` / `Permission denied` | AVG sets `SSLKEYLOGFILE` |
| `CERTIFICATE_VERIFY_FAILED` | Python/requests can't verify GitHub/PyPI |
| `esptool` / `setuptools` / `UnknownIssuer` | **uv** (esptool installer) can't reach PyPI |

Run once from project root:

```powershell
cd c:\Projects\wireclaw\WireClaw
.\scripts\setup-pio-ssl.ps1
.\scripts\bootstrap-platform.ps1
```

What `setup-pio-ssl.ps1` does:

- Clears `SSLKEYLOGFILE` (AVG SSL proxy pipe)
- Installs **pip-system-certs** offline (Python uses Windows cert store)
- Installs **setuptools** and **wheel** offline (esptool build deps)
- Pre-installs optional **freertos-gdb** (silences repeated build warnings)
- Creates `sitecustomize.py` in PlatformIO's Python

`flash.py` and `pio.ps1` also set **`UV_NATIVE_TLS=1`**, **`PYTHONUTF8=1`**, and **`CI=1`** for reliable uploads on Windows.

---

## After flashing — status & benchmarks

WireClaw exposes the same core metrics in three places: **onboard LCD**, **web UI**, and **serial**.

### 1. Onboard LCD (Waveshare ESP32-C6-LCD-1.47 only)

Build with `esp32-c6-lcd` (default). The screen updates every **2 seconds** and shows:

| Line | Metric |
|------|--------|
| Header | WireClaw version |
| IP | WiFi IP (or `connecting...`) |
| Heap | Free / total KB |
| Min heap | Lowest free heap since boot |
| Chip temp | °C (green / orange / red by temperature) |
| CPU | MHz |
| Device | `device_name` + WiFi RSSI |
| Uptime | Hours, minutes, seconds |
| Last LLM | Last call duration + token counts |
| LLM | Total calls + history turns |
| Model | Configured LLM model |

If the backlight is on but text is missing, re-flash with `esp32-c6-lcd` (not `esp32-c6`). If fully dark, check USB power and press **RESET**.

### 2. Web config portal

Open in a browser on the same WiFi network:

```
http://wireclaw-01.local/
```

(or `http://<device-ip>/` — see [Find device IP](#find-device-ip))

The **Status** tab opens by default and auto-refreshes every **5 seconds**. Sections:

**Device:** version, device name, uptime, IP, WiFi SSID/RSSI, model, NATS, Telegram

**Benchmarks:** heap free/total/min, CPU MHz, flash size, chip temp, LLM call count, last LLM time/tokens, history turns, rules, registered devices

Use **Refresh** manually or switch tabs and back. Hard-refresh the browser (Ctrl+F5) if you see an old empty page after updating firmware.

### 3. Serial monitor (115200 baud)

```powershell
python scripts\flash.py --port COM3 --monitor
```

On boot you should see:

```
WiFi: IP = 192.168.x.x
--- Status (web: http://192.168.x.x/ Status tab) ---
Heap: ...
CPU: ...
Chip temp: ...
```

Type **`/status`** anytime for a full text dump (WiFi, heap, CPU, flash, temp, LLM stats, rules, uptime).

Other useful commands: `/help`, `/devices`, `/rules`, `/heap`, `/config`, `/setup` (WiFi setup portal).

### RGB LED heartbeat (GPIO8 on LCD board)

When the LED is not overridden by a tool/rule:

| Color | Meaning |
|-------|---------|
| Cyan pulse | Normal (chip cool) |
| Orange pulse | Warm (≥ 45 °C) |
| Red pulse | Hot (≥ 55 °C) |

---

## Find device IP

After WiFi connects:

### Serial monitor

```powershell
python scripts\flash.py --port COM3 --monitor
```

Look for `WiFi: IP = 192.168.x.x` on boot, or type `/status`.

### Web / mDNS

```
http://<device_name>.local/
```

(e.g. `http://wireclaw-01.local/` from `device_name` in `config.json`)

The web **Status** tab shows **IP Address**.

### Telegram

If Telegram is configured, WireClaw sends a startup message:

```
WireClaw v... started
Config: http://192.168.x.x/
mDNS: http://wireclaw-01.local/
```

### Router

Check your router's **connected devices / DHCP** list for the ESP32 or `device_name`.

### No IP?

WiFi may not be connected. Verify `wifi_ssid` / `wifi_pass` in `config.json`, reboot, and watch serial for errors. If config is missing or wrong, the device starts **WireClaw-Setup** AP — connect and configure from the captive portal, or type `/setup` on serial.

---

## Troubleshooting

### esptool / setuptools / `UnknownIssuer`

```
Failed to build `esptool`
No solution found when resolving: `setuptools>=64`
invalid peer certificate: UnknownIssuer
```

```powershell
.\scripts\setup-pio-ssl.ps1
python scripts\flash.py --port COM3
```

### `freertos-gdb` warnings (harmless)

```
Warning: Failed to install freertos-gdb into ... tool-riscv32-esp-elf-gdb ...
```

Optional GDB debug support only — **upload still succeeds**. Re-run `.\scripts\setup-pio-ssl.ps1` to pre-install offline.

### UnicodeEncodeError during upload (`cp1252`)

```
UnicodeEncodeError: 'charmap' codec can't encode characters ...
```

esptool's Unicode progress bar breaks on Windows cp1252. Use `flash.py` or `pio.ps1` (they set `PYTHONUTF8=1`, `CI=1`, UTF-8 console). Or manually:

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
$env:CI = "1"
$env:NO_COLOR = "1"
python scripts\flash.py --port COM3
```

### Plain `pio` fails but scripts work

Use `python scripts\flash.py` or `.\scripts\pio.ps1` — not bare `pio run` on Windows with AVG.

### Still failing on HTTPS downloads

Temporarily disable **AVG → Web Shield / HTTPS scanning**, run setup + flash once, then re-enable.

### LCD blank after flash

1. Confirm you flashed **`esp32-c6-lcd`**, not `esp32-c6`:
   ```powershell
   python scripts\flash.py --port COM3 -e esp32-c6-lcd
   ```
2. Press **RESET** on the board; wait for WiFi (IP appears on LCD within ~30 s).
3. Backlight on but no text → re-flash firmware (step 1).
4. Fully dark → check USB cable/power.

### Web Status tab empty

1. Open `http://<device-ip>/` on the **same WiFi** as the ESP32.
2. Hard-refresh (Ctrl+F5).
3. Click **Status** or wait — it loads automatically and refreshes every 5 s.
4. If you see "Could not load status", the browser cannot reach `/api/status` — verify IP and WiFi.

### PlatformIO SSL / platform download fails

```powershell
.\scripts\setup-pio-ssl.ps1
.\scripts\bootstrap-platform.ps1
python scripts\flash.py --port COM3
```

---

## Summary of WireClaw enhancements (this fork)

- **`scripts/flash.py`** — Python flash tool with COM auto-detect, step progress, Windows SSL/UTF-8 fixes
- **`scripts/pio.ps1`** — PlatformIO wrapper for AVG/Windows
- **`scripts/setup-pio-ssl.ps1`** — Offline SSL and build-deps fix for PlatformIO Python
- **`esp32-c6-lcd` env** — Waveshare 1.47" ST7789 onboard display driver + GPIO8 RGB LED
- **LCD status screen** — IP, heap, temp, CPU, uptime, LLM benchmarks (2 s refresh)
- **Web Status tab** — Benchmarks section, default tab, 5 s auto-refresh, improved error handling
- **Serial `/status`** — Full benchmark dump; boot-time status summary after WiFi connect
- **Temperature LED heartbeat** — Cyan / orange / red by chip temperature
