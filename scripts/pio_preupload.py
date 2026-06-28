"""PlatformIO pre-upload hook — probe serial port before esptool opens it.

Runs immediately before upload/uploadfs (after the build step), so a stale
COM3 from docs/CLI does not fail only at esptool time without guidance.

Registered from platformio.ini:
  extra_scripts = pre:scripts/pio_preupload.py
"""

Import("env")

import sys
import time
from pathlib import Path

PROJECT_DIR = Path(env["PROJECT_DIR"])
PORT_CACHE = PROJECT_DIR / ".pio" / "wireclaw_upload_port.txt"

KNOWN_VIDS = {0x303A, 0x10C4, 0x1A86, 0x0403}
PORT_HINTS = ("esp32", "espressif", "usb serial", "usb jtag", "serial debug", "uart", "cp210", "ch340", "ftdi")


def _list_ports():
    seen = set()
    out = []

    def add(device, desc=""):
        if not device:
            return
        key = device.upper()
        if key in seen:
            return
        seen.add(key)
        out.append((device, desc or "unknown"))

    try:
        from serial.tools import list_ports

        for p in list_ports.comports():
            add(p.device, p.description or "")
    except ImportError:
        pass

    if sys.platform == "win32":
        try:
            import subprocess

            names = subprocess.check_output(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    "[System.IO.Ports.SerialPort]::getportnames() -join '|'",
                ],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            for name in names.split("|"):
                add(name.strip(), "Windows serial port")
        except Exception:
            pass

    return sorted(out, key=lambda x: x[0])


def _win_port(name):
    if sys.platform == "win32" and not name.startswith("\\\\."):
        return f"\\\\.\\{name}"
    return name


def _probe(port):
    try:
        import serial

        ser = serial.Serial(_win_port(port), baudrate=115200, timeout=0.3)
        ser.close()
        return True, "OK"
    except Exception as exc:
        err = str(exc).lower()
        if "access is denied" in err or "permission" in err:
            return False, "port busy — close serial monitor / PuTTY / other app"
        if "filenotfound" in err or "cannot find" in err or "no such file" in err:
            return False, "port not found — check USB cable and Device Manager"
        return False, str(exc)


def _score_port(desc, hwid, vid):
    score = 0
    blob = f"{desc} {hwid}".lower()
    if vid in KNOWN_VIDS:
        score += 50
    for hint in PORT_HINTS:
        if hint in blob:
            score += 10
    if "bluetooth" in blob:
        score -= 100
    return score


def _auto_detect():
    candidates = []
    try:
        from serial.tools import list_ports
        import re

        for p in list_ports.comports():
            hwid = (p.hwid or "").lower()
            desc = (p.description or "").lower()
            vid = p.vid
            if vid is None:
                m = re.search(r"vid:pid=([0-9a-f]{4}):", hwid)
                if m:
                    vid = int(m.group(1), 16)
            candidates.append((_score_port(desc, hwid, vid), p.device, desc or hwid))
    except ImportError:
        pass

    if not candidates:
        listed = _list_ports()
        if len(listed) == 1:
            return listed[0][0]
        return None

    candidates.sort(key=lambda x: (-x[0], x[1]))
    best_score, best_port, _ = candidates[0]
    if len(candidates) > 1 and best_score <= 0:
        return None
    return best_port


def _cache_port(port):
    try:
        PORT_CACHE.parent.mkdir(parents=True, exist_ok=True)
        PORT_CACHE.write_text(port, encoding="utf-8")
    except OSError:
        pass


def _set_upload_port(port):
    env.Replace(UPLOAD_PORT=port)
    try:
        env.Replace(MONITOR_PORT=port)
    except Exception:
        pass
    _cache_port(port)


def _print_help():
    print("\nWireClaw: serial upload port unavailable.")
    print("  • Close serial monitor / PuTTY / Cursor terminal using the port")
    print("  • Re-plug USB (data cable, not charge-only) and press RESET")
    print("  • List ports:  python scripts\\flash.py --list-ports")
    print("  • Auto-detect:  python scripts\\flash.py   (omit --port COM3)")
    print("\nAvailable ports:")
    listed = _list_ports()
    if not listed:
        print("  (none — connect the ESP32 via USB)")
    for port, desc in listed:
        print(f"  {port}  ({desc})")


def before_upload(source, target, env):
    upload_port = env.subst("$UPLOAD_PORT").strip()

    if not upload_port:
        detected = _auto_detect()
        if not detected:
            _print_help()
            env.Exit(1)
        print(f"WireClaw: auto-detected upload port {detected}")
        _set_upload_port(detected)
        upload_port = detected

    for attempt in range(1, 4):
        ok, msg = _probe(upload_port)
        if ok:
            _cache_port(upload_port)
            if attempt > 1:
                print(f"WireClaw: port {upload_port} ready (attempt {attempt}/3)")
            return

        print(f"WireClaw: {upload_port} not ready (attempt {attempt}/3): {msg}")

        alt = _auto_detect()
        if alt and alt.upper() != upload_port.upper():
            ok_alt, msg_alt = _probe(alt)
            if ok_alt:
                print(f"WireClaw: switching upload port {upload_port} -> {alt}")
                _set_upload_port(alt)
                upload_port = alt
                continue
            print(f"WireClaw: alternate port {alt} also failed: {msg_alt}")

        if attempt < 3:
            time.sleep(2)

    _print_help()
    env.Exit(1)


env.AddPreAction("upload", before_upload)
env.AddPreAction("uploadfs", before_upload)
