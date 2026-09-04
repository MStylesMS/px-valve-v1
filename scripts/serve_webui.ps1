# Serve the valve admin UI for local iteration (demo mocks, no ESP).
# Open http://127.0.0.1:8092/index.html
# 8090 = px-wifi-v1, 8091 = px-fuse-v1.
$ErrorActionPreference = "Stop"
$port = 8092
$webui = Join-Path $PSScriptRoot "..\main\webui"
Set-Location $webui
Write-Host "Serving $((Resolve-Path $webui).Path) on http://127.0.0.1:$port/"
Write-Host "Demo mocks auto-enable on localhost. Ctrl+C to stop."
python -m http.server $port --bind 127.0.0.1
