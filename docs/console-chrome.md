# Prop console chrome — px-valve-v1

Copied from [px-fuse-v1/docs/console-chrome.md](../../px-fuse-v1/docs/console-chrome.md) /
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md), then
specialized for the valve panel. Theme: **Signal Glass**.

## Pages

| Page | File | Job |
|------|------|-----|
| Live | `index.html` | Valve cards: name, colour circle, seats, small ID. Basic I/O shows 0–7 L→R then T→B. Valve Order follows Config order, hides disabled, dims waiting cards. Target Positions shows enabled IDs and hides unused. Enable / disable / forceScan. Puzzle modes: start/reset + solution string. |
| Config | `config.html` | Game mode (`io` / `valve_order` / `target_positions`); per-valve On / Name / Seats / Color / Order / Target; scan / debounce / settle / heartbeat / HTTP Debug. No CF/IC pin-map dropdown. |
| Monitor | `monitor.html` | Per-valve reeds (valve 0 A/B raw bits) and 3× MCP23S17 pin grid. No SPI/MQTT last-payload panel — Connect holds broker/topics. |
| Connect | `connection.html` | Wi-Fi, MQTT, mDNS/identity, OTA link |
| OTA | `update.html` | Firmware upload from Connect |
| Samples | `samples.html` | Dummy-data scenarios for UI review (not shipped later) |

Tab order: **Live, Config, Monitor, Connect**.

## Look

Same CSS tokens as px-wifi-v1 Signal Glass. Valve extras live at the bottom of
`styles.css` (cards, seat boxes, MCP tables).


## Responsive (required)

Must work on phone (~390px), tablet (~768px), and desktop. Shared chrome rules
live in `styles.css` and are documented in
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md):

- `max-width: 820px` � stack `.layout` **and** `.layout.live-layout`; wrap tabs
- `max-width: 520px` � phone padding / single-column metrics; prop-specific grids

Do not ship UI changes without checking those widths. Bootstrap is optional.

## Local UI iteration (no flash)

```powershell
# from px-valve-v1/
.\scripts\serve_webui.ps1
```

http://127.0.0.1:8092/index.html — demo mocks auto-on for localhost.

Scenario query: `?scenario=nominal|disabled|ab-off|ab-a|ab-b|path-mid|path-rewind|solved|targets`
