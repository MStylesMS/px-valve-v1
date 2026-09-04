# Upload firmware via HTTP OTA.
# Live Valve32Prop (Arduino-era): POST /ota  (-Legacy)
# After px-valve-v1 is on the unit: POST /api/ota/upload
#
# Usage:
#   .\scripts\ota_upload.ps1 -HostAddress 192.168.8.50 -Legacy
#   .\scripts\ota_upload.ps1 -HostAddress 192.168.8.50
param(
    [string]$HostAddress = "",
    [string]$Url = "",
    [string]$Bin = "",
    [switch]$Legacy
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Bin) {
    $Bin = Join-Path $root "build\px-valve-v1.bin"
}
if (-not (Test-Path $Bin)) {
    throw "Missing firmware binary: $Bin (run idf.py build first)"
}
if (-not $Url) {
    if (-not $HostAddress) {
        throw "Pass -HostAddress <ip-or-mdns> or -Url http://.../path"
    }
    if ($Legacy) {
        $Url = "http://$HostAddress/ota"
    } else {
        $Url = "http://$HostAddress/api/ota/upload"
    }
}

$len = (Get-Item $Bin).Length
Write-Host "Uploading $Bin ($len bytes) -> $Url"
# Classic ESP32 with SoftAP+STA often drops bulk TCP; --limit-rate helps.
& curl.exe --fail --show-error --connect-timeout 15 --max-time 600 `
    --limit-rate 8k `
    -X POST --data-binary "@$Bin" `
    -H "Content-Type: application/octet-stream" `
    $Url
if ($LASTEXITCODE -ne 0) {
    throw "OTA upload failed (curl exit $LASTEXITCODE)"
}
Write-Host ""
Write-Host "OTA upload finished. Device should reboot shortly."
