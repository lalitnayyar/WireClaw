# WireClaw — Install & Flash (Windows / ESP32-C6)

Guide for building, flashing, and using WireClaw on ESP32-C6 boards — including the **Waveshare ESP32-C6-LCD-1.47** with a professional centered status UI, live clock, rainbow owner name, phone number, full `/status` metrics on-screen, and Telegram alert overlays.

For WireClaw capabilities (AI agent, rules, Telegram, NATS), see [README.md](README.md).

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

### LCD feature overview

Centered professional layout — identity header refreshes every **1 second** (live NTP clock); status metrics match serial **`/status`** and the web **Status** tab:

```
        ─────────────
        WIRECLAW
         v0.x.x
    L A L I T  N A Y Y A R    ← owner_name (rainbow, centered)
        ─────────────
        14:32:08              ← current time (timezone from config)
      SUN 28 JUN 2026         ← date
    +971508320336             ← owner_phone (centered)
        ─────────────
  WIFI OK (192.168.68.143)
       -68dBM  wireclaw-01
  HEAP 144920 / 252652
  MIN 90576
  CPU 160MHZ  FL 8192KB
  TEMP 41.7 C                 ← green / orange / red by temp
  HIST 0  LLM 0
  LAST 0ms (0+0 tok)
  RULES 0 ACTIVE
  TG ENABLED
  UPTIME 98s
  google/gemini-2.5-flash
```

Telegram message → **8 s** centered alert overlay (identity header stays visible):

```
        ─────────────
    (name / clock / phone)
      ┌ TELEGRAM IN ─┐   green = received
      │ message...   │   orange = sent
      RETURN IN 6s
```

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
python scripts\flash.py
```

Replace `COM3` with your port if auto-detect fails (see below).

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
| `owner_name` | Your name on the LCD header (rainbow colors, centered), e.g. `Lalit Nayyar` |
| `owner_phone` | Phone number shown centered on LCD, e.g. `+971508320336` |
| `telegram_token` / `telegram_chat_id` | Optional Telegram bot (enables LCD alert overlays) |
| `telegram_cooldown` | Minimum seconds between rule-triggered Telegram sends |
| `timezone` | POSIX TZ string for NTP clock on LCD (e.g. `UTC4`, `GST-4`) |

`owner_name`, `owner_phone`, and `timezone` affect the LCD display. After editing, reflash the filesystem:

```powershell
python scripts\flash.py --fs-only
```

Other fields changed via the web UI require **Save + Reboot**. Initial upload always needs a filesystem flash (`uploadfs` or `flash.py` without `--firmware-only`).

---

## Flash firmware + config

### Python script (recommended)

`scripts/flash.py` — **interactive** flash with port selection, live progress (`>>` lines), and a final report. Auto-detects COM port, applies Windows SSL/UTF-8 fixes.

```powershell
# Interactive (default in a terminal): pick port, confirm plan, see report
python scripts\flash.py

# Non-interactive / CI
python scripts\flash.py --port COM5 -y --no-interactive

# Waveshare LCD board (default env: esp32-c6-lcd)
python scripts\flash.py

# Plain ESP32-C6 DevKit (no onboard LCD)
python scripts\flash.py -e esp32-c6

# Other options
python scripts\flash.py --list-ports
python scripts\flash.py --monitor      # flash + serial monitor (115200)
python scripts\flash.py --firmware-only # skip LittleFS / config
python scripts\flash.py --fs-only       # config only
python scripts\flash.py -e esp32-s3 --port COM5     # different chip
```

**What gets flashed:**

1. **Firmware** — WireClaw application (PlatformIO `upload`)
2. **Filesystem** — `data/config.json` and optional `system_prompt.txt` (PlatformIO `uploadfs`)

### Flash progress output

Interactive mode shows a banner, flash plan, and confirmation before starting. During build/upload, key lines are echoed with a `>>` prefix:

```
  ╔══════════════════════════════════════════════════════════╗
  ║              WireClaw Interactive Flash                  ║
  ...

  Flash plan
  ────────────────────────────────────────
  Environment : esp32-c6-lcd
  Serial port : COM5
  Steps       : Build & upload firmware → Upload LittleFS ...

==============================================================
  [1/2] Build & upload firmware
==============================================================
  >> Compiling …/src/main.cpp.o
  >> RAM usage: 60.5% (198,096 / 327,680 bytes)
  >> Flash usage: 52.7% (1,380,432 / 2,621,440 bytes)
  >> Uploading …/firmware.bin
  >> Flash write: 0x00010000
  [1/2] Build & upload firmware — OK (45.2s)

==============================================================
  FLASH REPORT
==============================================================
  Status          SUCCESS
  Environment     esp32-c6-lcd
  Total time      1m 12s
  Phases
  ✓ [1] Build & upload firmware (upload) — 45.2s
  ✓ [2] Upload filesystem … (uploadfs) — 27.1s
  Next steps: monitor, web UI, LCD, Telegram /status
```

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

Build with `esp32-c6-lcd` (default).

#### Professional status layout (normal view)

Refreshes every **1 second** (live clock + metrics). All primary text is **centered**. The status block mirrors serial **`/status`** output:

| LCD line | Same as `/status` |
|----------|-------------------|
| `WIFI OK (192.168.x.x)` | WiFi connected + IP |
| `-68dBM wireclaw-01` | RSSI + device name |
| `HEAP 144920 / 252652` | Heap free / total (bytes) |
| `MIN 90576` | Minimum heap since boot |
| `CPU 160MHZ FL 8192KB` | CPU frequency + flash size |
| `TEMP 41.7 C` | Chip temperature (color-coded) |
| `HIST 0 LLM 0` | History turns + LLM call count |
| `LAST 0ms (0+0 tok)` | Last LLM latency + prompt/completion tokens |
| `RULES 0 ACTIVE` | Active automation rules |
| `TG ENABLED` / `TG OFF` | Telegram bot status |
| `UPTIME 98s` | Seconds since boot |
| `google/gemini-2.5-flash` | Model from config (truncated if long) |

```
        ─────────────
        WIRECLAW
         v0.x.x
    L A L I T  N A Y Y A R    ← rainbow owner_name
        ─────────────
        14:32:08              ← current time (NTP)
      SUN 28 JUN 2026         ← date
    +971508320336             ← owner_phone
        ─────────────
  WIFI OK (192.168.68.143)
       -68dBM  wireclaw-01
  HEAP 144920 / 252652
  MIN 90576
  CPU 160MHZ  FL 8192KB
  TEMP 41.7 C
  HIST 0  LLM 0
  LAST 1200ms (120+45 tok)
  RULES 2 ACTIVE
  TG ENABLED
  UPTIME 98s
  google/gemini-2.5-flash
```

#### Personal info on LCD (`owner_name` + `owner_phone`)

Set in `config.json`:

```json
"owner_name": "Lalit Nayyar",
"owner_phone": "+971508320336",
"timezone": "UTC4"
```

| Field | LCD display |
|-------|-------------|
| `owner_name` | Centered **rainbow** text — each letter a different color |
| `owner_phone` | Centered below the date in cyan |
| `timezone` | Controls the live clock and date (NTP synced on boot) |

Leave either field empty to hide that line. Update after changes:

```powershell
python scripts\flash.py --fs-only
```

#### Telegram alerts on LCD

When Telegram is enabled, the LCD switches to a **centered alert overlay** for **8 seconds** (your name, clock, and phone stay visible at the top):

| Direction | Banner | When |
|-----------|--------|------|
| **IN** | Green **TELEGRAM IN** | You send a message to the bot (chat, commands, `/status`) |
| **OUT** | Orange **TELEGRAM OUT** | WireClaw sends a reply, rule alert, or startup notification |

Message text is centered and wrapped to fit the screen. Footer shows **`RETURN IN Xs`**, then the status dashboard returns automatically.

Rule-based Telegram alerts (e.g. chip temp > 40 °C) trigger **TELEGRAM OUT** on the LCD as well as on your phone.

#### LCD troubleshooting

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
python scripts\flash.py --monitor
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
python scripts\flash.py --monitor
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

### COM port not found / upload failed

If you see:

```
Could not open COM3, the port is busy or doesn't exist
FileNotFoundError
```

The **build succeeded** — only esptool v5.3.0 could not open the serial port at upload time.

WireClaw now probes the port **immediately before upload** (`scripts/pio_preupload.py`) and auto-switches if `--port COM3` is stale. Firmware is **compiled first**, then the port is checked — so you are not left guessing after a long build.

Fix:

1. **Plug in** the ESP32 via USB (data cable, not charge-only)
2. **Close** any serial monitor using the port (Cursor terminal, `pio device monitor`, PuTTY)
3. **Find** the correct port:
   ```powershell
   python scripts\flash.py --list-ports
   ```
4. **Flash** with auto-detect (recommended — omit `--port`):
   ```powershell
   python scripts\flash.py
   ```
   Or use the port from step 3:
   ```powershell
   python scripts\flash.py --port COM5
   ```

COM numbers change when you swap USB ports or replug the board — **do not hard-code COM3** from old docs.

### `pip_system_certs` / `avgMonFltProxy` warning

If you see:

```
pip_system_certs: ERROR: could not inject truststore ... avgMonFltProxy
```

This is AVG antivirus setting `SSLKEYLOGFILE`. Re-run setup (it clears the variable and patches `sitecustomize.py`):

```powershell
.\scripts\setup-pio-ssl.ps1
python scripts\flash.py
```

Flash can still succeed if the build completes; the warning is harmless when deps are already installed.

### esptool / setuptools / `UnknownIssuer`

```
Failed to build `esptool`
No solution found when resolving: `setuptools>=64`
invalid peer certificate: UnknownIssuer
```

```powershell
.\scripts\setup-pio-ssl.ps1
python scripts\flash.py
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
python scripts\flash.py
```

### Plain `pio` fails but scripts work

Use `python scripts\flash.py` or `.\scripts\pio.ps1` — not bare `pio run` on Windows with AVG.

### Still failing on HTTPS downloads

Temporarily disable **AVG → Web Shield / HTTPS scanning**, run setup + flash once, then re-enable.

### LCD blank after flash

1. Confirm you flashed **`esp32-c6-lcd`**, not `esp32-c6`:
   ```powershell
   python scripts\flash.py -e esp32-c6-lcd
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
python scripts\flash.py
```

---

## Summary of WireClaw enhancements (this fork)

### Flash & tooling

- **`scripts/flash.py`** — Python flash tool with COM auto-detect, step progress, Windows SSL/UTF-8 fixes
- **`scripts/pio.ps1`** — PlatformIO wrapper for AVG/Windows
- **`scripts/setup-pio-ssl.ps1`** — Offline SSL and build-deps fix for PlatformIO Python
- **`scripts/bootstrap-platform.ps1`** — One-time ESP32 platform bootstrap
- **`esp32-c6-lcd` env** — Default build for Waveshare 1.47" board (8 MB flash)

### Onboard LCD (Waveshare ESP32-C6-LCD-1.47)

Built-in **ST7789** driver (`src/st7789_ws147.cpp`, `src/lcd_display.cpp`) — no TFT_eSPI dependency.

| Feature | Detail |
|---------|--------|
| **Professional UI** | Centered layout with accent dividers, identity header, and full status metrics block |
| **Live clock & date** | NTP time (`HH:MM:SS`) and date — refreshes every **1 s**; uses `timezone` from config |
| **Rainbow owner name** | `owner_name` centered under header (each letter a different color) |
| **Owner phone** | `owner_phone` centered below date (e.g. `+971508320336`) |
| **WiFi & network** | Connected IP, RSSI (dBm), and device name |
| **Heap** | Free / total bytes and minimum heap since boot |
| **CPU & flash** | CPU MHz and flash size (KB) |
| **Chip temperature** | Degrees C with green / orange (≥ 45 °C) / red (≥ 55 °C) |
| **LLM stats** | History turns, LLM call count, last call ms, prompt+completion tokens |
| **Rules & Telegram** | Active rule count; `TG ENABLED` or `TG OFF` |
| **Uptime & model** | Seconds since boot; configured model name (truncated if needed) |
| **Telegram IN alert** | Green centered banner + message when Telegram received — **8 s** overlay |
| **Telegram OUT alert** | Orange centered banner when WireClaw sends Telegram — **8 s** overlay |
| **Alert countdown** | Footer shows `RETURN IN Xs` until status view returns |
| **RGB LED** | GPIO8 WS2812 — heartbeat cyan / orange / red by chip temperature |

Change `owner_name`, `owner_phone`, or `timezone` in `config.json`, then:

```powershell
python scripts\flash.py --fs-only
```

`owner_name` and `owner_phone` are configured in `config.json` only (not yet in the web UI).

### Web & serial status

- **Web Status tab** — Benchmarks section, default tab, 5 s auto-refresh, cache-busting, error handling
- **Serial `/status`** — Full benchmark dump; boot-time status summary after WiFi connect
- **Temperature LED heartbeat** — Cyan / orange / red by chip temperature (DevKit or LCD board)

See also [README.md](README.md) for WireClaw features, rule engine, and Telegram integration.
