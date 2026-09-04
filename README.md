# px-valve-v1

ESP32 valve-panel firmware — Paradox TFD generator room (Valve32Prop).

Classic **esp32**, ESP-IDF **6.0.x**. Current version is in `version.txt`.

## License

Dual-licensed:

- **AGPL-3.0** for open source use — see [LICENSE](LICENSE).
- **Commercial license required** for proprietary or revenue-generating use that does not comply with AGPL-3.0 — see [COMMERCIAL.md](COMMERCIAL.md).

Copyright © 2026 Mark Stevens.

## ESP-IDF

Pin **ESP-IDF 6.0.x** (stable 6.0.3 or newer 6.0 bugfix). Shared components live in the sibling `px-components` checkout (`PX_COMPONENTS_VERSION`). Override with `PX_COMPONENTS_DIR` if the layout is not `../px-components`.

```powershell
# After export.ps1 for IDF 6.0.x:
idf.py set-target esp32
idf.py build
```

First OTA onto a live Arduino-era valve32: `.\scripts\ota_upload.ps1 -HostAddress <ip> -Legacy`  
Later flashes: `.\scripts\ota_upload.ps1 -HostAddress <ip>`

## Local UI preview

```powershell
.\scripts\serve_webui.ps1
```

- http://127.0.0.1:8092/index.html — Live
- http://127.0.0.1:8092/config.html — Config (game mode + settings A–H)
- http://127.0.0.1:8092/monitor.html — Monitor (reeds + MCP grid)
- http://127.0.0.1:8092/connection.html — Connect
- http://127.0.0.1:8092/samples.html — dummy-data scenarios

`localhost` enables demo mocks automatically. `?demo=1` / `?demo=0` override.

Port **8092** so it does not collide with px-wifi-v1 (8090) or px-fuse-v1 (8091).

## Documentation

- [Prop console chrome](docs/console-chrome.md) — Live / Config / Connect page contract
- [Changelog](CHANGELOG.md)
