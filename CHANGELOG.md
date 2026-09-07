# Changelog

All notable changes to px-valve-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

## [Unreleased]

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
