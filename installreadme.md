# WireClaw — Install & Flash (Windows / ESP32-C6)

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

Use the COM port in flash commands, e.g. `--port COM3`.

---

## One-time setup (Windows + AVG antivirus)

AVG can break PlatformIO HTTPS in several ways:

| Symptom | Cause |
|---------|--------|
| `avgMonFltProxy` / `Permission denied` | AVG sets `SSLKEYLOGFILE` |
| `CERTIFICATE_VERIFY_FAILED` | Python/requests can't verify GitHub/PyPI |
| `esptool` / `setuptools` / `UnknownIssuer` | **uv** (used to install esptool) can't reach PyPI |

Run these once from the project root:

```powershell
cd c:\Projects\wireclaw\WireClaw
.\scripts\setup-pio-ssl.ps1
.\scripts\bootstrap-platform.ps1
```

What `setup-pio-ssl.ps1` does:

- Clears `SSLKEYLOGFILE` (AVG SSL proxy pipe)
- Installs **pip-system-certs** offline (Python uses Windows cert store)
- Installs **setuptools** and **wheel** offline (esptool build deps)
- Creates `sitecustomize.py` in PlatformIO's Python

`flash.py` and `pio.ps1` also set **`UV_NATIVE_TLS=1`** so uv/esptool can reach PyPI via Windows certs.

Copy config if needed:

```powershell
copy data\config.json.example data\config.json
# Edit data\config.json with your WiFi and API key
```

---

## Flash firmware + config

```powershell
.\scripts\setup-pio-ssl.ps1
python scripts\flash.py --port COM3
```

Options:

```powershell
python scripts\flash.py --port COM3 --monitor      # flash + serial monitor
python scripts\flash.py --port COM3 --firmware-only
python scripts\flash.py --port COM3 --fs-only      # config only
```

### Flash progress

`flash.py` shows step-by-step progress:

```
============================================================
[1/2] Build & upload firmware
============================================================
> pio.exe run -e esp32-c6 -v -t upload --upload-port COM3 ...
  >> Compiling .pio/build/esp32-c6/src/main.cpp.o
  >> Uploading .pio/build/esp32-c6/firmware.bin
  >> Writing at 0x00010000... (100 %)

============================================================
[2/2] Upload filesystem (config + prompt)
============================================================
...
============================================================
Flash complete.
============================================================
```

- Step headers: `[1/2]`, `[2/2]`, etc.
- Full PlatformIO verbose output (`-v`)
- Highlight lines with compile/upload progress (`>> ...`)

The first build can take several minutes while tools download. Later builds are much faster.

Manual equivalent:

```powershell
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
$env:UV_NATIVE_TLS = "1"
.\scripts\pio.ps1 run -e esp32-c6 -t upload --upload-port COM3
.\scripts\pio.ps1 run -e esp32-c6 -t uploadfs --upload-port COM3
.\scripts\pio.ps1 device monitor -e esp32-c6 --port COM3 -b 115200
```

---

## Troubleshooting

### esptool / setuptools / `UnknownIssuer`

If you see:

```
Failed to build `esptool`
No solution found when resolving: `setuptools>=64`
invalid peer certificate: UnknownIssuer
```

Re-run the SSL setup, then flash again:

```powershell
.\scripts\setup-pio-ssl.ps1
python scripts\flash.py --port COM3
```

### `freertos-gdb` warnings (harmless)

If you see repeated:

```
Warning: Failed to install freertos-gdb into ... tool-riscv32-esp-elf-gdb ...
```

This is optional GDB debug support only — **firmware and filesystem upload still succeed**.
Re-run setup to pre-install it offline:

```powershell
.\scripts\setup-pio-ssl.ps1
```

### UnicodeEncodeError during upload (`cp1252` / progress bar)

If upload crashes with:

```
UnicodeEncodeError: 'charmap' codec can't encode characters ...
```

esptool's progress bar uses Unicode block characters that Windows cp1252 can't print.
`flash.py` and `pio.ps1` fix this automatically (`PYTHONUTF8=1`, `CI=1`, UTF-8 console).

Retry:

```powershell
python scripts\flash.py --port COM3
```

Or set manually before plain `pio`:

```powershell
chcp 65001
$env:PYTHONUTF8 = "1"
$env:CI = "1"
$env:NO_COLOR = "1"
```

### Plain `pio` fails but scripts work

Always use `python scripts\flash.py` or `.\scripts\pio.ps1` instead of bare `pio run` on Windows with AVG. The wrappers clear `SSLKEYLOGFILE` and set `UV_NATIVE_TLS=1`.

### Still failing on HTTPS downloads

Temporarily disable **AVG → Web Shield / HTTPS scanning**, run setup + flash once, then re-enable.

---

## After flashing

- Serial monitor: **115200 baud**
- Web config: `http://<device-ip>/` → **Status** tab (benchmarks)
- LED heartbeat: cyan = normal, orange = warm, red = hot
