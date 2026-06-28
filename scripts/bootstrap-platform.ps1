# One-time bootstrap: download pioarduino platform with curl (works with AVG on Windows)
# and install into ~/.platformio/platforms/espressif32
$ErrorActionPreference = "Stop"
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
Remove-Item Env:SSLKEYLOG -ErrorAction SilentlyContinue

$zip = Join-Path $env:TEMP "platform-espressif32.zip"
$extract = Join-Path $env:TEMP "platform-espressif32-extract"
$platformDir = Join-Path $env:USERPROFILE ".platformio\platforms\espressif32"
$url = "https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip"

Write-Host "Downloading platform from GitHub..."
curl.exe --ssl-no-revoke -L -o $zip $url
if (-not (Test-Path $zip) -or (Get-Item $zip).Length -lt 1000000) {
    Write-Error "Download failed or file too small: $zip"
}

if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
New-Item -ItemType Directory -Path $extract | Out-Null
tar -xf $zip -C $extract

$src = Get-ChildItem $extract -Directory | Select-Object -First 1
if (-not $src) { Write-Error "Could not find extracted platform folder" }

if (Test-Path $platformDir) {
    $backup = "$platformDir.bak.$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Write-Host "Backing up existing platform to $backup"
    Move-Item $platformDir $backup
}

Move-Item $src.FullName $platformDir
Write-Host "Installed platform to $platformDir"
Get-Content (Join-Path $platformDir "platform.json") | Select-String '"version"'
