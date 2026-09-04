# px-valve-v1 — AI Instructions

TFD generator-room sequential valve firmware for Paradox escape rooms.

## Status

Firmware + Signal Glass console for **Valve32Prop** (`.50`). Default game mode
is **Basic I/O** so Node `valve3.js` keeps the path. Do not enable Valve Order
/ Target Positions while valve3.js is running. Config holds **eight settings
A–H per mode**; **A** emulates Crafty Fox `valve4.js`.

## Firmware

- Target: classic **esp32**, IDF **6.0.x**, version from `version.txt`.
- `EXTRA_COMPONENT_DIRS` → `../px-components`.
- SoftAP form: `Paradox-PXValveV1-XXXX`.
- Default mDNS hostname: **`valve.local`** (`networkName`; change via Connect or `POST /api/device/name`).
- Default STA: `Paradox-TFD-1` / venue PSK; broker `192.168.8.130`.
- First OTA onto live valve32: `.\scripts\ota_upload.ps1 -HostAddress 192.168.8.50 -Legacy`
- Later: `.\scripts\ota_upload.ps1 -HostAddress 192.168.8.50`

Local UI preview without flash:

```powershell
.\scripts\serve_webui.ps1
```

→ http://127.0.0.1:8092/index.html (wifi-v1 = 8090, fuse = 8091).

Scenario mocks: http://127.0.0.1:8092/samples.html

Admin UI chrome: [docs/console-chrome.md](docs/console-chrome.md) (must work on
phone / tablet / desktop — see Responsive section). Plan:
[rooms/tfd/docs/ESP32-VALVE-PLAN.md](../../../rooms/tfd/docs/ESP32-VALVE-PLAN.md).

## MQTT (legacy Prop, required in I/O mode)

| Topic | Payload |
|-------|---------|
| `/Paradox/TFD/Valve/Prop/Commands` | `enable` / `disable` / `forceScan`, `{Valve,Inlet}` |
| `/Paradox/TFD/Valve/Prop/Events` | `{Valve, Position}` |
| `/Paradox/Props` | heartbeat id `Valve32Prop` |

Do not rewrite `valve3.js` / `generator.js` unless asked.
Accept `command` as an alias of `Command`; publish PascalCase.

## Other conventions

- Do not edit `props/esp32/archive/valve32` or `tfd-old` runtime.
- Version bump default `+0.01`.
- Path-relative assets + `lib_http_proxy` when serving embedded UI.

## Suite standards

Public suite brief + contracts: [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/).
