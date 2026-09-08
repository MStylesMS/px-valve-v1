# Changelog

All notable changes to px-valve-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

## [Unreleased]

## [0.11] - 2026-09-07

### Fixed

- MCP probe never reached the bus: IDF 6 rejects `spi_device_polling_start`
  with a finite timeout, so every transfer errored and chips 0–2 always read
  as missing (root cause of the 0.07 "not found" banner on the live panel).
  Transfers now use `spi_device_queue_trans` + `get_trans_result` (80 ms).
- Scan task slept `pdMS_TO_TICKS(5)` = 0 ticks at 100 Hz, starving IDLE and
  tripping the task WDT on a bench chip. Sleep is now ≥1 tick.

## [0.10] - 2026-09-07

### Fixed

- SoftAP and STA now start **before** the MCP scan task. 0.09 created
  `valve_scan` at priority 6 first; SPI reads on a bare bus never returned,
  so `web_ui_start()` never ran and neither STA nor `Paradox-PXValveV1-*`
  came up.
- SPI uses `SPI_DMA_DISABLED` and timed `polling_start`/`end` (80 ms).
  Missing expanders fail the IODIRA probe instead of hanging the CPU.
- Panel I/O stays off and every page shows the red `hwFault` banner until
  chips 0–2 answer. Re-probe every 500 ms after seating.

## [0.09] - 2026-09-07

### Fixed

- Restored the 0.06 MCP path: talk to chips 0–2 after SPI init, no
  write/readback presence probe (0.07/0.08 probes false-failed the live panel
  and 0.08 could block boot before Wi-Fi).
- MCP init runs from the scan task after SoftAP/STA start so a wedged SPI
  bus cannot keep the console offline.

## [0.08] - 2026-09-07

### Fixed

- MCP presence probe no longer uses `spi_device_polling_start/end` or OLATA
  readback. Those rejected the live 3-chip panel (same as the first patch
  0.13 miss). Probe IODIRA with the 0.06 `polling_transmit` path.

## [0.07] - 2026-09-07

### Fixed

- Missing MCP23S17 expanders (bare DevKit / unseated panel) no longer block
  boot or trip the scan-task watchdog. Wi-Fi and SoftAP always come up.
  A red banner on every console page reports the hardware fault. Panel I/O
  stays disabled until all three chips answer a write/readback.

## [0.06] - 2026-09-07

### Added

- GM overrides: `solveValve` (one valve / advance path) and `solve` (whole
  puzzle finale). Live buttons for each valve ID and Solve valve puzzle.
  `reset` clears GM forced steps.

## [0.05] - 2026-09-06

### Fixed

- Position debounce now requires consecutive identical samples (was any
  mix of “not last published”). Brief target hits are published instead
  of being reported as OFF or the next seat.
- Default scan 5 ms / debounce 2 (about 10 ms to accept a seat, 15 ms to
  report OFF). Setting A has white (ID 7) back on after the yellow/white
  body swap.

## [0.04] - 2026-09-06

### Changed

- Valve Order / Target Positions match [VALVE-LOGIC.md](../../rooms/tfd/docs/VALVE-LOGIC.md):
  A or OFF → B; leave-target unwinds downstream but keeps colour targets;
  colour inlet is the seat at first enable; Target Positions stay dark until
  the host enables each valve. Prop Events include `Enabled` and `Inlet`.
  Setting A names match the panel: ID 1 RED, ID 7 YEL (play order A/B, red,
  yellow, white, blue, black, green).
  **Do not OTA until asked. Stop Node valve scripts before using these modes.**

## [0.03] - 2026-09-06

### Added

- Valve Order / Target Positions engine on the prop: start/reset/stop/getSolution
  on `/Paradox/TFD/Valve/Commands`, `"solved"` / `{Solution}` / `{State}` on
  Events, whoosh via ImageSwitcher. **Stop `valve3.js` before using these modes.**

### Fixed

- A–B (2-seat): never target OFF. Only A or B solves; passing through OFF holds
  the last seated state and does not rewind the path. Solved A–B lights Inlet 3.

## [0.02] - 2026-09-04

### Added

- Config: eight game settings **A–H per mode**. Select a letter, edit the
  valve table, Apply / Apply + Save. **A** is the Crafty Fox `valve4.js`
  game (order 0→1→7→3→5→2→6, valve 4 off, A-B/YEL/BLK/WHT/BLU/GRN/RED,
  RND targets).
- Dropdowns use dark navy text on a light field so native option lists stay
  readable.

## [0.01] - 2026-09-03

### Added

- Signal Glass prop console (Live / Config / Monitor / Connect / OTA) copied from
  px-fuse-v1 / px-wifi-v1 and specialized for the valve panel.
- Dummy-data scenarios: nominal I/O, disabled, A–B off/A/B, Valve Order
  mid-path and solved, Target Positions. Local preview on port **8092**.
- Config game-mode control (`io` / `valve_order` / `target_positions`).

### Changed

- Live cards: IDs 0–7 left-to-right then top-to-bottom in Basic I/O (no path
  caption). Valve Order follows Config order. Target Positions hides unused.
- Seat chrome: unmatched target is black/green; current+target is green fill;
  current-only stays yellow. Colour circle behind seats. Valve ID bottom-left.
- Config: per-valve On / Name / Seats / Color / Order / Target. Pin-map
  dropdown and Monitor SPI/MQTT payload panel removed.
- Valve Order Live: waiting cards dimmed; only the active step is bright.
  Leaving an earlier target puts later valves back to waiting.
- Target **RND** replaces the empty “—”: pick a different seat on activate.
- Duplicate Order numbers highlight yellow with white text until unique.
- Basic I/O shows all eight cards at full brightness (no waiting dim).
- Solved seat outline is green (extra ring so the valve-color circle cannot
  fringe red around the box).
