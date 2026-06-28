# Run PlatformIO with AVG/Windows SSL + UTF-8 console fixes.
$ErrorActionPreference = "Stop"
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
Remove-Item Env:SSLKEYLOG -ErrorAction SilentlyContinue

# uv (used to install esptool) must use Windows cert store, not bundled Mozilla CA
$env:UV_NATIVE_TLS = "1"

# esptool upload progress uses Unicode; cp1252 + PlatformIO click.secho crashes
chcp 65001 | Out-Null
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
$env:CI = "1"
$env:NO_COLOR = "1"
$env:FORCE_COLOR = "0"

$setup = Join-Path $PSScriptRoot "setup-pio-ssl.ps1"
& $setup

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) {
    $pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
}
if (-not $pio) {
    Write-Error "PlatformIO not found. Install the PlatformIO IDE extension or: pip install platformio"
}

& $pio @args
