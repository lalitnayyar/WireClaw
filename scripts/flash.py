#!/usr/bin/env python3
"""
Interactive WireClaw flash tool — build, upload firmware + LittleFS, final report.

Uses PlatformIO from the project root. Auto-detects the serial port when
--port is omitted (Windows COM*, Linux /dev/ttyACM*, macOS /dev/cu.*).

Examples:
  python scripts/flash.py                  # interactive (TTY) + auto port
  python scripts/flash.py --port COM5      # explicit port
  python scripts/flash.py --firmware-only  # skip LittleFS upload
  python scripts/flash.py --fs-only        # config / prompt only
  python scripts/flash.py --list-ports
  python scripts/flash.py -y --no-interactive  # CI / non-interactive
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = PROJECT_ROOT / "data" / "config.json"
PORT_CACHE = PROJECT_ROOT / ".pio" / "wireclaw_upload_port.txt"

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

ENV_LABELS = {
    "esp32-c6-lcd": "Waveshare ESP32-C6-LCD-1.47 (ST7789 display)",
    "esp32-c6": "ESP32-C6 DevKit (no onboard LCD)",
    "esp32-s3": "ESP32-S3",
    "esp32-c3": "ESP32-C3",
}


@dataclass
class PhaseResult:
    name: str
    target: str
    started: float = field(default_factory=time.time)
    ended: float | None = None
    exit_code: int | None = None
    highlights: list[str] = field(default_factory=list)

    @property
    def duration_s(self) -> float:
        end = self.ended if self.ended is not None else time.time()
        return max(0.0, end - self.started)

    @property
    def ok(self) -> bool:
        return self.exit_code == 0


@dataclass
class BuildMetrics:
    ram: str | None = None
    flash: str | None = None
    pio_duration: str | None = None


@dataclass
class FlashReport:
    env: str
    port: str
    firmware: bool
    filesystem: bool
    phases: list[PhaseResult] = field(default_factory=list)
    metrics: BuildMetrics = field(default_factory=BuildMetrics)
    config_summary: dict[str, str] = field(default_factory=dict)
    started: float = field(default_factory=time.time)
    ended: float | None = None
    success: bool = False

    @property
    def total_duration_s(self) -> float:
        end = self.ended if self.ended is not None else time.time()
        return max(0.0, end - self.started)


def fmt_duration(seconds: float) -> str:
    if seconds < 60:
        return f"{seconds:.1f}s"
    m, s = divmod(int(seconds), 60)
    if m < 60:
        return f"{m}m {s}s"
    h, m = divmod(m, 60)
    return f"{h}h {m}m {s}s"


def find_pio_cmd() -> list[str] | None:
    candidates: list[list[str]] = []

    for name in ("pio", "platformio"):
        path = shutil.which(name)
        if path:
            candidates.append([path])

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


def ensure_pio_ssl_windows(*, quiet: bool = False) -> None:
    if sys.platform != "win32":
        return

    setup = PROJECT_ROOT / "scripts" / "setup-pio-ssl.ps1"
    if not setup.exists():
        return

    if not quiet:
        print("  • Checking PlatformIO SSL & build deps (Windows)...", flush=True)
    rc = subprocess.call(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(setup)],
        cwd=PROJECT_ROOT,
        stdout=subprocess.DEVNULL if quiet else None,
        stderr=subprocess.DEVNULL if quiet else None,
    )
    if rc != 0 and not quiet:
        print(
            "Warning: PlatformIO SSL setup failed. Try: .\\scripts\\setup-pio-ssl.ps1",
            file=sys.stderr,
        )


def configure_windows_console() -> None:
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
    env = os.environ.copy()
    for key in ("SSLKEYLOGFILE", "SSLKEYLOG"):
        env.pop(key, None)

    if sys.platform == "win32":
        env["UV_NATIVE_TLS"] = "1"
        env["PYTHONUTF8"] = "1"
        env["PYTHONIOENCODING"] = "utf-8"
        env["CI"] = "1"
        env["NO_COLOR"] = "1"
        env["FORCE_COLOR"] = "0"

    return env


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


def list_all_ports(pio_cmd: list[str]) -> list[tuple[str, str]]:
    seen: set[str] = set()
    out: list[tuple[str, str]] = []

    def add(port: str | None, desc: str) -> None:
        if not port:
            return
        key = port.upper()
        if key in seen:
            return
        seen.add(key)
        out.append((port, desc or "unknown"))

    for entry in list_ports_pio(pio_cmd):
        add(entry.get("port") or entry.get("path"), entry.get("description") or "")

    for entry in list_ports_pyserial():
        add(entry.get("port"), entry.get("description") or "")

    if sys.platform == "win32":
        try:
            ps = subprocess.check_output(
                [
                    "powershell",
                    "-NoProfile",
                    "-Command",
                    "[System.IO.Ports.SerialPort]::getportnames() -join '|'",
                ],
                text=True,
                stderr=subprocess.DEVNULL,
            ).strip()
            for name in ps.split("|"):
                add(name.strip(), "Windows serial port")
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

    return sorted(out, key=lambda x: x[0])


def port_exists(port: str, pio_cmd: list[str]) -> bool:
    want = port.upper()
    return any(p.upper() == want for p, _ in list_all_ports(pio_cmd))


def win_serial_port(port: str) -> str:
    if sys.platform == "win32" and not port.startswith("\\\\."):
        return f"\\\\.\\{port}"
    return port


def probe_serial_port(port: str) -> tuple[bool, str]:
    """Open the port briefly — catches missing COM and busy port before esptool."""
    try:
        import serial  # type: ignore

        ser = serial.Serial(win_serial_port(port), baudrate=115200, timeout=0.3)
        ser.close()
        return True, "OK"
    except ImportError:
        return True, "probe skipped (pyserial unavailable)"
    except Exception as exc:
        err = str(exc).lower()
        if "access is denied" in err or "permission" in err:
            return False, "port busy — close serial monitor / PuTTY / other app"
        if "filenotfound" in err or "cannot find" in err or "no such file" in err:
            return False, "port not found — check USB cable and Device Manager"
        return False, str(exc)


def read_cached_upload_port() -> str | None:
    try:
        value = PORT_CACHE.read_text(encoding="utf-8").strip()
        return value or None
    except OSError:
        return None


def ensure_upload_port(pio_cmd: list[str], port: str | None) -> str | None:
    """Verify port immediately before upload; fall back to auto-detect."""
    if not port:
        detected = detect_port(pio_cmd, announce=True)
        if detected:
            ok, msg = probe_serial_port(detected)
            if ok:
                return detected
            print(f"  Auto-detected {detected} but cannot open: {msg}", file=sys.stderr)
        print_port_list(pio_cmd)
        return None

    for attempt in range(1, 4):
        ok, msg = probe_serial_port(port)
        if ok:
            if attempt > 1:
                print(f"  Port {port} ready (attempt {attempt}/3)")
            return port

        print(f"  Port {port} not ready ({attempt}/3): {msg}", file=sys.stderr)
        alt = detect_port(pio_cmd, announce=False)
        if alt and alt.upper() != port.upper():
            ok_alt, msg_alt = probe_serial_port(alt)
            if ok_alt:
                print(f"  Switching upload port {port} -> {alt}")
                return alt
            print(f"  Alternate {alt} also failed: {msg_alt}", file=sys.stderr)

        if attempt < 3:
            time.sleep(2)

    print_port_list(pio_cmd)
    return None


def print_port_list(pio_cmd: list[str], *, to_stderr: bool = True) -> None:
    stream = sys.stderr if to_stderr else sys.stdout
    ports = list_all_ports(pio_cmd)
    if not ports:
        print(
            "  (no serial ports found — connect USB and press RESET on the board)",
            file=stream,
        )
        return
    print("Available ports:", file=stream)
    for port, desc in ports:
        print(f"  {port}  ({desc})", file=stream)


def detect_port(pio_cmd: list[str], *, announce: bool = True) -> str | None:
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
        return None

    if announce and len(candidates) > 1:
        print(f"  Auto-selected {best_port} ({best_desc or 'serial device'})")
        others = [c for c in candidates[1:3] if c[0] >= best_score - 5]
        if others:
            alt = ", ".join(p for _, p, _ in others)
            print(f"  (also matched: {alt})")

    return best_port


def load_config_summary() -> dict[str, str]:
    if not CONFIG_PATH.exists():
        return {"status": "missing — run full flash to upload data/config.json"}
    try:
        data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {"status": "invalid JSON in data/config.json"}

    keys = (
        "device_name",
        "owner_name",
        "owner_phone",
        "timezone",
        "model",
        "wifi_ssid",
    )
    out: dict[str, str] = {}
    for key in keys:
        val = data.get(key)
        if val:
            out[key] = str(val)
    if "wifi_ssid" in out:
        out["wifi_ssid"] = out["wifi_ssid"][:4] + "…" if len(out["wifi_ssid"]) > 4 else out["wifi_ssid"]
    return out


def print_banner() -> None:
    print()
    print("  ╔══════════════════════════════════════════════════════════╗")
    print("  ║              WireClaw Interactive Flash                  ║")
    print("  ║     ESP32 firmware + LittleFS (config & system prompt)   ║")
    print("  ╚══════════════════════════════════════════════════════════╝")
    print()


def prompt_yes_no(question: str, *, default: bool = True) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    while True:
        try:
            ans = input(f"  {question} {suffix} ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return False
        if not ans:
            return default
        if ans in ("y", "yes"):
            return True
        if ans in ("n", "no"):
            return False
        print("  Please enter y or n.")


def prompt_port_choice(pio_cmd: list[str]) -> str | None:
    ports = list_all_ports(pio_cmd)
    if not ports:
        return None

    if len(ports) == 1:
        port, desc = ports[0]
        print(f"  Found one port: {port} ({desc})")
        if prompt_yes_no(f"Use {port}?", default=True):
            return port
        return None

    print("  Multiple serial ports detected:")
    for i, (port, desc) in enumerate(ports, 1):
        marker = " *" if _score_port(desc.lower(), desc.lower(), None) > 0 else ""
        print(f"    [{i}] {port}  —  {desc}{marker}")
    print("    [0] Cancel")

    while True:
        try:
            raw = input("  Select port number: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return None
        if raw == "0":
            return None
        if raw.isdigit():
            idx = int(raw)
            if 1 <= idx <= len(ports):
                return ports[idx - 1][0]
        print("  Invalid choice — enter a number from the list.")


def wait_for_board(pio_cmd: list[str], timeout_s: int = 90) -> str | None:
    print(f"  Waiting for ESP32 on USB (up to {timeout_s}s)...")
    print("  Plug in the board and press RESET if needed.")
    deadline = time.time() + timeout_s
    last_count = -1
    while time.time() < deadline:
        ports = list_all_ports(pio_cmd)
        if ports:
            if len(ports) == 1:
                port, desc = ports[0]
                print(f"  Board detected: {port} ({desc})")
                return port
            detected = detect_port(pio_cmd, announce=False)
            if detected:
                print(f"  Board detected: {detected}")
                return detected
            return prompt_port_choice(pio_cmd)

        count = len(ports)
        if count != last_count:
            print(f"  … still waiting ({int(deadline - time.time())}s left)", flush=True)
            last_count = count
        time.sleep(2)

    print("  Timed out — no serial port appeared.")
    return None


def resolve_port(
    pio_cmd: list[str],
    user_port: str | None,
    *,
    interactive: bool,
) -> str | None:
    if user_port:
        if port_exists(user_port, pio_cmd):
            ok, msg = probe_serial_port(user_port)
            if ok:
                return user_port
            print(f"\n  Warning: {user_port} is listed but not open: {msg}", file=sys.stderr)
        else:
            print(f"\n  Warning: port {user_port} not found.", file=sys.stderr)
            print_port_list(pio_cmd)

        detected = detect_port(pio_cmd, announce=False)
        if detected and detected.upper() != user_port.upper():
            ok, _ = probe_serial_port(detected)
            if ok:
                print(
                    f"\n  Using {detected} instead of {user_port} "
                    f"(omit --port next time: python scripts\\flash.py)",
                    file=sys.stderr,
                )
                return detected

        print(
            "\n  Fix: close serial monitor, re-plug USB, then run without --port:\n"
            "    python scripts\\flash.py",
            file=sys.stderr,
        )
        return None

    if interactive:
        port = detect_port(pio_cmd, announce=False)
        if port and prompt_yes_no(f"Use auto-detected port {port}?", default=True):
            ok, msg = probe_serial_port(port)
            if ok:
                return port
            print(f"  Cannot open {port}: {msg}")
        chosen = prompt_port_choice(pio_cmd)
        if chosen:
            return chosen
        return wait_for_board(pio_cmd)

    port = detect_port(pio_cmd)
    if not port:
        print("\n  Could not detect ESP32 serial port.", file=sys.stderr)
        print_port_list(pio_cmd)
        print(
            "\n  Connect USB and run:  python scripts\\flash.py",
            file=sys.stderr,
        )
    elif not probe_serial_port(port)[0]:
        print(f"\n  Port {port} detected but not open — close other serial apps.", file=sys.stderr)
        return None
    return port


def show_flash_plan(report: FlashReport) -> None:
    env_label = ENV_LABELS.get(report.env, report.env)
    steps: list[str] = []
    if report.firmware:
        steps.append("Build & upload firmware")
    if report.filesystem:
        steps.append("Upload LittleFS (config + system prompt)")

    print("  Flash plan")
    print("  ────────────────────────────────────────")
    print(f"  Environment : {report.env}")
    print(f"                {env_label}")
    print(f"  Serial port : {report.port}")
    print(f"  Steps       : {' → '.join(steps) if steps else '(none)'}")
    if report.config_summary:
        print("  Config preview (data/config.json):")
        for key, val in report.config_summary.items():
            print(f"    {key}: {val}")
    print("  ────────────────────────────────────────")
    print()


_PROGRESS_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"Compiling\s+(.+)", re.I), "Compiling"),
    (re.compile(r"Linking\s+(.+)", re.I), "Linking"),
    (re.compile(r"Building\s+(.+)", re.I), "Building"),
    (re.compile(r"Uploading\s+(.+)", re.I), "Uploading"),
    (re.compile(r"Writing at\s+(0x[0-9a-f]+)", re.I), "Flash write"),
    (re.compile(r"Hash of data verified", re.I), "Verify OK"),
    (re.compile(r"Hard resetting", re.I), "Resetting device"),
    (re.compile(r"Leaving\.\.\.", re.I), "Upload complete"),
]


def parse_pio_line(line: str, phase: PhaseResult, metrics: BuildMetrics) -> str | None:
    stripped = line.rstrip()
    if not stripped:
        return None

    ram_m = re.search(
        r"RAM:\s+\[[^\]]+\]\s+([\d.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)",
        stripped,
    )
    if ram_m:
        metrics.ram = f"{ram_m.group(1)} ({int(ram_m.group(2)):,} / {int(ram_m.group(3)):,} bytes)"
        return f"RAM usage: {metrics.ram}"

    flash_m = re.search(
        r"Flash:\s+\[[^\]]+\]\s+([\d.]+%)\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)",
        stripped,
    )
    if flash_m:
        metrics.flash = (
            f"{flash_m.group(1)} ({int(flash_m.group(2)):,} / {int(flash_m.group(3)):,} bytes)"
        )
        return f"Flash usage: {metrics.flash}"

    took_m = re.search(r"\[SUCCESS\]\s+Took\s+([\d.]+)\s+seconds", stripped, re.I)
    if took_m:
        metrics.pio_duration = f"{took_m.group(1)}s"
        return f"PlatformIO finished in {metrics.pio_duration}"

    took_m2 = re.search(r"Took\s+([\d.]+)\s+seconds", stripped, re.I)
    if took_m2 and "[FAILED]" not in stripped:
        metrics.pio_duration = f"{took_m2.group(1)}s"

    for pattern, label in _PROGRESS_PATTERNS:
        m = pattern.search(stripped)
        if not m:
            continue
        detail = m.group(1).strip() if m.lastindex else ""
        if len(detail) > 72:
            detail = "…" + detail[-69:]
        msg = f"{label}: {detail}" if detail else label
        if msg not in phase.highlights:
            phase.highlights.append(msg)
        return msg

    if "[FAILED]" in stripped or "Error" in stripped and "fatal error" in stripped.lower():
        short = stripped if len(stripped) <= 100 else stripped[:97] + "…"
        phase.highlights.append(short)
        return short

    return None


def run_tracked(
    cmd: list[str],
    *,
    cwd: Path,
    phase: PhaseResult,
    metrics: BuildMetrics,
    step_label: str,
    step_index: int,
    step_total: int,
) -> int:
    print()
    print("=" * 62)
    print(f"  [{step_index}/{step_total}] {step_label}")
    print("=" * 62)
    print(f"  > {' '.join(cmd)}")
    print()

    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=pio_subprocess_env(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )

    assert proc.stdout is not None
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        highlight = parse_pio_line(line, phase, metrics)
        if highlight:
            print(f"  >> {highlight}", flush=True)

    rc = proc.wait()
    phase.ended = time.time()
    phase.exit_code = rc

    status = "OK" if rc == 0 else f"FAILED (exit {rc})"
    print()
    print(f"  [{step_index}/{step_total}] {step_label} — {status} ({fmt_duration(phase.duration_s)})")
    print()

    return rc


def pio_build(
    pio_cmd: list[str],
    env: str,
    *,
    phase: PhaseResult,
    metrics: BuildMetrics,
    step_label: str,
    step_index: int,
    step_total: int,
    extra_target: str | None = None,
) -> int:
    cmd = pio_cmd + ["run", "-e", env, "-v"]
    if extra_target:
        cmd.extend(["-t", extra_target])
    return run_tracked(
        cmd,
        cwd=PROJECT_ROOT,
        phase=phase,
        metrics=metrics,
        step_label=step_label,
        step_index=step_index,
        step_total=step_total,
    )


def pio_upload(
    pio_cmd: list[str],
    env: str,
    port: str | None,
    target: str,
    *,
    phase: PhaseResult,
    metrics: BuildMetrics,
    step_label: str,
    step_index: int,
    step_total: int,
) -> tuple[int, str | None]:
    ready = ensure_upload_port(pio_cmd, port)
    if port and not ready:
        return 1, port

    cmd = pio_cmd + ["run", "-e", env, "-v"]
    upload_port = ready or port
    if upload_port:
        cmd.extend(["--upload-port", upload_port, "--monitor-port", upload_port])
    cmd.extend(["-t", target])
    rc = run_tracked(
        cmd,
        cwd=PROJECT_ROOT,
        phase=phase,
        metrics=metrics,
        step_label=step_label,
        step_index=step_index,
        step_total=step_total,
    )
    cached = read_cached_upload_port()
    return rc, cached or upload_port


def print_final_report(report: FlashReport) -> None:
    report.ended = time.time()
    width = 62

    def row(label: str, value: str) -> None:
        print(f"  {label:<16} {value}")

    print()
    print("=" * width)
    title = " FLASH REPORT "
    pad = (width - len(title)) // 2
    print(" " * pad + title)
    print("=" * width)

    if report.success:
        print("  Status          SUCCESS")
    else:
        print("  Status          FAILED")

    row("Environment", report.env)
    row("Board", ENV_LABELS.get(report.env, "—"))
    row("Serial port", report.port)
    row("Total time", fmt_duration(report.total_duration_s))

    if report.metrics.ram:
        row("RAM", report.metrics.ram)
    if report.metrics.flash:
        row("Flash", report.metrics.flash)

    print()
    print("  Phases")
    print("  ────────────────────────────────────────")
    for i, phase in enumerate(report.phases, 1):
        mark = "✓" if phase.ok else "✗"
        print(
            f"  {mark} [{i}] {phase.name} ({phase.target}) — "
            f"{fmt_duration(phase.duration_s)}"
        )
        for note in phase.highlights[-4:]:
            print(f"       • {note}")

    if report.success:
        print()
        print("  Flashed")
        print("  ────────────────────────────────────────")
        if report.firmware:
            print("  • Application firmware (bootloader + partitions + app)")
        if report.filesystem:
            print("  • LittleFS: data/config.json, system_prompt.txt (if present)")

        if report.config_summary.get("device_name"):
            print()
            print("  Next steps")
            print("  ────────────────────────────────────────")
            print("  1. Open serial monitor:")
            print(f"     python scripts\\flash.py --port {report.port} --monitor")
            print("  2. Connect to WiFi AP or join your network (see serial log)")
            print("  3. Web config: http://<device-ip>/  (Status tab for benchmarks)")
            if report.env == "esp32-c6-lcd":
                print("  4. LCD should show owner name, clock, uptime, and metrics")
            print("  5. Telegram: send /status to your bot for a full dump")

    else:
        print()
        print("  Troubleshooting")
        print("  ────────────────────────────────────────")
        print("  • List ports:  python scripts\\flash.py --list-ports")
        print("  • Close serial monitor / PuTTY holding the COM port")
        print("  • Re-plug USB (data cable) and press RESET on the board")
        print("  • Retry:       python scripts\\flash.py")

    print("=" * width)
    print()


def build_step_plan(args: argparse.Namespace) -> tuple[bool, bool, list[tuple[str, str, str]]]:
    """Return (firmware, filesystem, [(phase_key, pio_target, label), ...])."""
    if args.fs_only and args.firmware_only:
        raise ValueError("Choose either --fs-only or --firmware-only, not both.")

    if args.fs_only:
        return False, True, [
            ("filesystem", "uploadfs", "Upload filesystem (config + prompt)"),
        ]
    if args.firmware_only:
        return True, False, [
            ("firmware", "upload", "Build & upload firmware"),
        ]
    return True, True, [
        ("firmware", "upload", "Build & upload firmware"),
        ("filesystem", "uploadfs", "Upload filesystem (config + prompt)"),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Interactive build and flash for WireClaw ESP32 boards."
    )
    parser.add_argument(
        "-e",
        "--env",
        default="esp32-c6-lcd",
        help="PlatformIO environment (default: esp32-c6-lcd)",
    )
    parser.add_argument(
        "-p",
        "--port",
        help="Serial port (e.g. COM5). Auto-detected or prompted if omitted.",
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
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List serial ports and exit",
    )
    parser.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="Interactive prompts (default when run in a terminal)",
    )
    parser.add_argument(
        "--no-interactive",
        action="store_true",
        help="Skip prompts (use with --port for scripts/CI)",
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="Skip confirmation prompt before flashing",
    )
    args = parser.parse_args()

    configure_windows_console()

    pio_cmd = find_pio_cmd()
    if not pio_cmd:
        print("PlatformIO not found. Install with: pip install platformio", file=sys.stderr)
        return 1

    if args.list_ports:
        print_port_list(pio_cmd, to_stderr=False)
        ports = list_all_ports(pio_cmd)
        return 0 if ports else 1

    interactive = args.interactive or (sys.stdin.isatty() and not args.no_interactive)

    if interactive:
        print_banner()
        print("  Preparing...")
        ensure_pio_ssl_windows(quiet=True)
        print("  PlatformIO ready.")
        print()
    else:
        ensure_pio_ssl_windows()

    port = resolve_port(pio_cmd, args.port, interactive=interactive)
    if not port:
        if interactive:
            print_final_report(
                FlashReport(
                    env=args.env,
                    port=args.port or "—",
                    firmware=not args.fs_only,
                    filesystem=not args.firmware_only and not args.fs_only,
                    success=False,
                )
            )
        return 1

    try:
        firmware, filesystem, steps = build_step_plan(args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if not steps:
        print("Nothing to flash.", file=sys.stderr)
        return 1

    report = FlashReport(
        env=args.env,
        port=port,
        firmware=firmware,
        filesystem=filesystem,
        config_summary=load_config_summary() if filesystem else {},
    )

    if interactive:
        show_flash_plan(report)
        if not args.yes and not prompt_yes_no("Proceed with flash?", default=True):
            print("  Cancelled.")
            return 0
    else:
        print(f"WireClaw flash  env={args.env}  port={port}  steps={len(steps)}")

    total = len(steps)
    step_idx = 0
    for phase_key, pio_target, label in steps:
        if pio_target == "upload":
            step_idx += 1
            build_phase = PhaseResult(name="Compile firmware", target="build")
            report.phases.append(build_phase)
            rc = pio_build(
                pio_cmd,
                args.env,
                phase=build_phase,
                metrics=report.metrics,
                step_label="Compile firmware (no upload yet)",
                step_index=step_idx,
                step_total=total + 1,
            )
            if rc != 0:
                report.success = False
                print_final_report(report)
                return rc

            step_idx += 1
            upload_phase = PhaseResult(name=label, target=pio_target)
            report.phases.append(upload_phase)
            rc, port = pio_upload(
                pio_cmd,
                args.env,
                port,
                pio_target,
                phase=upload_phase,
                metrics=report.metrics,
                step_label=label,
                step_index=step_idx,
                step_total=total + 1,
            )
            if port:
                report.port = port
        else:
            step_idx += 1
            phase = PhaseResult(name=label, target=pio_target)
            report.phases.append(phase)
            rc, port = pio_upload(
                pio_cmd,
                args.env,
                port,
                pio_target,
                phase=phase,
                metrics=report.metrics,
                step_label=label,
                step_index=step_idx,
                step_total=total,
            )
            if port:
                report.port = port

        if rc != 0:
            report.success = False
            print(
                "\n  Upload failed — esptool could not open the serial port.",
                file=sys.stderr,
            )
            print(
                "  Close serial monitor, re-plug USB, then:  python scripts\\flash.py",
                file=sys.stderr,
            )
            print_port_list(pio_cmd)
            print_final_report(report)
            return rc

    report.success = True
    print_final_report(report)

    if args.monitor:
        print("  Opening serial monitor (Ctrl+C to exit)...")
        print()
        rc = subprocess.call(
            pio_cmd + ["device", "monitor", "-e", args.env, "--port", port, "-b", "115200"],
            cwd=PROJECT_ROOT,
            env=pio_subprocess_env(),
        )
        return rc

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
