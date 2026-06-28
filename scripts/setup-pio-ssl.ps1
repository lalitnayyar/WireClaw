# Fix PlatformIO HTTPS on Windows (AVG SSLKEYLOGFILE + uv/pip cert issues).
$ErrorActionPreference = "Stop"
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
Remove-Item Env:SSLKEYLOG -ErrorAction SilentlyContinue

$pip = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pip.exe"
$py = Join-Path $env:USERPROFILE ".platformio\python3\python.exe"
if (-not (Test-Path $pip)) { Write-Error "PlatformIO pip not found at $pip" }

$wheelDir = Join-Path $env:TEMP "pio-ssl-wheels"
New-Item -ItemType Directory -Path $wheelDir -Force | Out-Null

$pyTag = & $py -c "import sys; print(f'cp{sys.version_info.major}{sys.version_info.minor}')"
Write-Host "PlatformIO Python tag: $pyTag"

function Download-Wheel($Package, $Pattern) {
    $raw = curl.exe --ssl-no-revoke -s "https://pypi.org/pypi/$Package/json"
    $json = $raw | ConvertFrom-Json
    $wheel = $json.urls | Where-Object {
        $_.packagetype -eq "bdist_wheel" -and $_.filename -match $Pattern
    } | Select-Object -First 1
    if (-not $wheel) { Write-Error "No wheel for $Package matching $Pattern" }

    $dest = Join-Path $wheelDir $wheel.filename
    if (-not (Test-Path $dest) -or (Get-Item $dest).Length -lt 1000) {
        Write-Host "Downloading $($wheel.filename)..."
        curl.exe --ssl-no-revoke -L -o $dest $wheel.url
    }
    if ((Get-Item $dest).Length -lt 1000) { Write-Error "Download failed: $($wheel.filename)" }
    return $dest
}

# pip-system-certs: Python requests/urllib3 use Windows trust store
Download-Wheel "pip-system-certs" "py3-none-any" | Out-Null
Download-Wheel "wrapt" "${pyTag}-${pyTag}-win_amd64" | Out-Null

# setuptools/wheel: esptool build via uv needs these (offline, no pypi SSL)
Download-Wheel "setuptools" "py3-none-any" | Out-Null
Download-Wheel "wheel" "py3-none-any" | Out-Null

Write-Host "Installing SSL + build deps into PlatformIO..."
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
Remove-Item Env:SSLKEYLOG -ErrorAction SilentlyContinue
& $pip install --no-index --find-links $wheelDir --upgrade `
    pip-system-certs wrapt setuptools wheel
if ($LASTEXITCODE -ne 0) { Write-Error "pip install failed" }

$sitecustomize = Join-Path $env:USERPROFILE ".platformio\penv\Lib\site-packages\sitecustomize.py"
@'
import os
# AVG sets SSLKEYLOGFILE to \\.\avgMonFltProxy\... which breaks pip/urllib3
os.environ.pop("SSLKEYLOGFILE", None)
os.environ.pop("SSLKEYLOG", None)
try:
    import pip_system_certs.wrapt_requests
except Exception:
    pass
'@ | Set-Content -Path $sitecustomize -Encoding utf8

Write-Host "PlatformIO SSL fix installed (pip-system-certs, setuptools, wheel)."

# freertos-gdb: optional GDB helper; pio tries uv pip each build (fails under AVG SSL)
$packagesDir = Join-Path $env:USERPROFILE ".platformio\packages"
$gdbTools = @("tool-riscv32-esp-elf-gdb", "tool-xtensa-esp-elf-gdb")
Download-Wheel "freertos-gdb" "py3-none-any" | Out-Null

foreach ($tool in $gdbTools) {
    $toolDir = Join-Path $packagesDir $tool
    if (-not (Test-Path $toolDir)) { continue }
    $target = Join-Path $toolDir "share\gdb\python"
    if (Test-Path (Join-Path $target "freertos_gdb")) {
        Write-Host "freertos-gdb already installed in $tool"
        continue
    }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Write-Host "Installing freertos-gdb into $target ..."
    & $pip install --no-index --find-links $wheelDir --target $target freertos-gdb
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "freertos-gdb install into $tool failed (GDB debug only; flash still works)"
    }
}

