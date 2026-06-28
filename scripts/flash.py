#!/usr/bin/env python3
"""
Flash WireClaw to a connected ESP32 (default: esp32-c6).

Uses PlatformIO from the project root. Auto-detects the serial port when
--port is omitted (Windows COM*, Linux /dev/ttyACM*, macOS /dev/cu.*).

Examples:
  python scripts/flash.py                  # firmware + filesystem
  python scripts/flash.py --port COM5      # explicit port (Windows)
  python scripts/flash.py --firmware-only  # skip LittleFS upload
  python scripts/flash.py -e esp32-s3      # different board target
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# Espressif + common USB-UART bridge vendors
KNOWN_VIDS = {
    0x303A,  # Espressif USB Serial/JTAG
    0x10C4,  # Silicon Labs CP210x
    0x1A86,  # WCH CH340
    0x0403,  # FTDI
}

PORT_HINTS = (
    "esp32",
    "espressif",
    "usb serial",
    "usb jtag",
    "serial debug",
    "uart",
    "cp210",
    "ch340",
    "ftdi",
)


def find_pio_cmd() -> list[str] | None:
    candidates: list[list[str]] = []

    for name in ("pio", "platformio"):
        path = shutil.which(name)
        if path:
            candidates.append([path])

    # PlatformIO IDE / Cursor extension default install (Windows)
    if sys.platform == "win32":
        pio_home = Path.home() / ".platformio" / "penv" / "Scripts"
        for name in ("pio.exe", "platformio.exe"):
            pio_exe = pio_home / name
            if pio_exe.exists():
                candidates.append([str(pio_exe)])

    for py in ("python", "python3"):
        path = shutil.which(py)
        if not path:
            continue
        try:
            subprocess.check_output(
                [path, "-m", "platformio", "--version"],
                stderr=subprocess.DEVNULL,
            )
            return [path, "-m", "platformio"]
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue

    return candidates[0] if candidates else None


def ensure_pio_ssl_windows() -> None:
    """Install Windows SSL + build deps for PlatformIO (AVG / esptool / uv)."""
    if sys.platform != "win32":
        return

    setup = PROJECT_ROOT / "scripts" / "setup-pio-ssl.ps1"
    if not setup.exists():
        return

    print("Checking PlatformIO SSL & build deps (Windows)...", flush=True)
    rc = subprocess.call(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(setup)],
        cwd=PROJECT_ROOT,
    )
    if rc != 0:
        print(
            "Warning: PlatformIO SSL setup failed. Try: .\\scripts\\setup-pio-ssl.ps1",
            file=sys.stderr,
        )


def configure_windows_console() -> None:
    """Use UTF-8 stdout so esptool progress bars don't crash PlatformIO on cp1252."""
    if sys.platform != "win32":
        return
    subprocess.call(
        "chcp 65001 >nul",
        shell=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
    except (AttributeError, OSError):
        pass


def pio_subprocess_env() -> dict[str, str]:
    """Subprocess env for PlatformIO — drop vars that break HTTPS on Windows."""
    env = os.environ.copy()
    # AVG (and some AV tools) set SSLKEYLOGFILE to \\.\avgMonFltProxy\...
    # urllib3 then fails with PermissionError when opening that pipe.
    for key in ("SSLKEYLOGFILE", "SSLKEYLOG"):
        env.pop(key, None)

    if sys.platform == "win32":
        # uv installs esptool during build; must trust Windows CA store (AVG MITM)
        env["UV_NATIVE_TLS"] = "1"
        # esptool progress uses Unicode block chars; cp1252 + click.secho crashes upload
        env["PYTHONUTF8"] = "1"
        env["PYTHONIOENCODING"] = "utf-8"
        env["CI"] = "1"
        env["NO_COLOR"] = "1"
        env["FORCE_COLOR"] = "0"

    return env


def run(cmd: list[str], *, cwd: Path, step: str | None = None) -> int:
    if step:
        print(f"\n{'=' * 60}", flush=True)
        print(step, flush=True)
        print(f"{'=' * 60}", flush=True)
    print(f"> {' '.join(cmd)}\n", flush=True)

    # Inherit stdout/stderr (no pipe) — piping breaks PlatformIO click + esptool Unicode on Windows
    return subprocess.call(cmd, cwd=cwd, env=pio_subprocess_env())


def list_ports_pio(pio_cmd: list[str]) -> list[dict]:
    try:
        out = subprocess.check_output(
            pio_cmd + ["device", "list", "--json-output"],
            cwd=PROJECT_ROOT,
            text=True,
            stderr=subprocess.DEVNULL,
            env=pio_subprocess_env(),
        )
        data = json.loads(out)
        return data if isinstance(data, list) else []
    except (subprocess.CalledProcessError, json.JSONDecodeError, FileNotFoundError):
        return []


def list_ports_pyserial() -> list[dict]:
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError:
        return []

    ports = []
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        vid = None
        m = re.search(r"vid:pid=([0-9a-f]{4}):", hwid)
        if m:
            vid = int(m.group(1), 16)
        ports.append(
            {
                "port": p.device,
                "description": p.description or "",
                "vid": vid,
                "score": _score_port(desc, hwid, vid),
            }
        )
    return ports


def _score_port(desc: str, hwid: str, vid: int | None) -> int:
    score = 0
    blob = f"{desc} {hwid}"
    if vid in KNOWN_VIDS:
        score += 50
    for hint in PORT_HINTS:
        if hint in blob:
            score += 10
    if "bluetooth" in blob:
        score -= 100
    return score


def detect_port(pio_cmd: list[str]) -> str | None:
    candidates: list[tuple[int, str, str]] = []

    for entry in list_ports_pio(pio_cmd):
        port = entry.get("port") or entry.get("path")
        if not port:
            continue
        desc = (entry.get("description") or "").lower()
        hwid = (entry.get("hwid") or "").lower()
        vid = None
        m = re.search(r"vid:pid=([0-9a-f]{4}):", hwid)
        if m:
            vid = int(m.group(1), 16)
        candidates.append((_score_port(desc, hwid, vid), port, desc or hwid))

    if not candidates:
        for entry in list_ports_pyserial():
            candidates.append((entry["score"], entry["port"], entry["description"]))

    if not candidates:
        return None

    candidates.sort(key=lambda x: (-x[0], x[1]))
    best_score, best_port, best_desc = candidates[0]

    if len(candidates) > 1 and best_score <= 0:
        print("Multiple serial ports found; specify one with --port:\n", file=sys.stderr)
        for score, port, desc in sorted(candidates, key=lambda x: x[1]):
            print(f"  {port}  ({desc or 'unknown'})", file=sys.stderr)
        return None

    if len(candidates) > 1:
        print(f"Auto-selected {best_port} ({best_desc or 'serial device'})")
        others = [c for c in candidates[1:3] if c[0] >= best_score - 5]
        if others:
            alt = ", ".join(p for _, p, _ in others)
            print(f"  (also matched: {alt}; override with --port if wrong)")

    return best_port


def pio_upload(
    pio_cmd: list[str],
    env: str,
    port: str | None,
    target: str,
    *,
    step: str,
) -> int:
    cmd = pio_cmd + ["run", "-e", env, "-v"]
    if port:
        cmd.extend(["--upload-port", port, "--monitor-port", port])
    cmd.extend(["-t", target])
    return run(cmd, cwd=PROJECT_ROOT, step=step)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and flash WireClaw to a connected ESP32."
    )
    parser.add_argument(
        "-e",
        "--env",
        default="esp32-c6",
        help="PlatformIO environment (default: esp32-c6)",
    )
    parser.add_argument(
        "-p",
        "--port",
        help="Serial port (e.g. COM5, /dev/ttyACM0). Auto-detected if omitted.",
    )
    parser.add_argument(
        "--firmware-only",
        action="store_true",
        help="Upload firmware only (skip LittleFS / config)",
    )
    parser.add_argument(
        "--fs-only",
        action="store_true",
        help="Upload LittleFS only (config + system prompt)",
    )
    parser.add_argument(
        "--monitor",
        action="store_true",
        help="Open serial monitor after flashing (115200 baud)",
    )
    args = parser.parse_args()

    configure_windows_console()
    ensure_pio_ssl_windows()

    pio_cmd = find_pio_cmd()
    if not pio_cmd:
        print(
            "PlatformIO not found. Install with: pip install platformio",
            file=sys.stderr,
        )
        return 1

    port = args.port
    if not port:
        port = detect_port(pio_cmd)
        if not port:
            print(
                "\nCould not detect ESP32 serial port. "
                "Connect the board and pass --port COMx (Windows) or /dev/ttyACM0 (Linux).",
                file=sys.stderr,
            )
            return 1

    print(f"WireClaw flash  env={args.env}  port={port}")

    if args.fs_only and args.firmware_only:
        print("Choose either --fs-only or --firmware-only, not both.", file=sys.stderr)
        return 1

    total_steps = (0 if args.fs_only else 1) + (0 if args.firmware_only else 1)
    step_num = 0

    if not args.fs_only:
        step_num += 1
        rc = pio_upload(
            pio_cmd,
            args.env,
            port,
            "upload",
            step=f"[{step_num}/{total_steps}] Build & upload firmware",
        )
        if rc != 0:
            return rc

    if not args.firmware_only:
        step_num += 1
        rc = pio_upload(
            pio_cmd,
            args.env,
            port,
            "uploadfs",
            step=f"[{step_num}/{total_steps}] Upload filesystem (config + prompt)",
        )
        if rc != 0:
            return rc

    print("\n" + "=" * 60)
    print("Flash complete.")
    print("=" * 60)

    if args.monitor:
        rc = run(
            pio_cmd + ["device", "monitor", "-e", args.env, "--port", port, "-b", "115200"],
            cwd=PROJECT_ROOT,
        )
        return rc

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
