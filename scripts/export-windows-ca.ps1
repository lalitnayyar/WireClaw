# Export Windows trusted root CAs for PlatformIO Python (fixes AVG HTTPS / CERTIFICATE_VERIFY_FAILED)
$ErrorActionPreference = "Stop"
Remove-Item Env:SSLKEYLOGFILE -ErrorAction SilentlyContinue
Remove-Item Env:SSLKEYLOG -ErrorAction SilentlyContinue

$out = Join-Path $env:TEMP "platformio-windows-ca.pem"
$lines = New-Object System.Collections.Generic.List[string]
$count = 0

foreach ($location in @("LocalMachine", "CurrentUser")) {
    $path = "Cert:\$location\Root"
    if (-not (Test-Path $path)) { continue }
    Get-ChildItem $path -ErrorAction SilentlyContinue | ForEach-Object {
        $lines.Add("-----BEGIN CERTIFICATE-----")
        $lines.Add([Convert]::ToBase64String($_.RawData, "InsertLineBreaks"))
        $lines.Add("-----END CERTIFICATE-----")
        $count++
    }
}

# Fallback: Python certifi bundle shipped with PlatformIO
$certifi = Join-Path $env:USERPROFILE ".platformio\penv\Lib\site-packages\certifi\cacert.pem"
if (Test-Path $certifi) {
    $lines.Add([System.IO.File]::ReadAllText($certifi))
    Write-Host "Appended certifi bundle from PlatformIO"
}

[System.IO.File]::WriteAllLines($out, $lines)
Write-Host "Wrote $count Windows root certs to $out"
Write-Output $out
