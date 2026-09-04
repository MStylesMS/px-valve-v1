(function () {
    const KEY_BASE = "px.api.base";
    const KEY_DEMO = "px.demo.mode";
    const KEY_CFG = "px.valve.demo.config";

    const PALETTE = [
        { id: "clear", hex: "", label: "Clear" },
        { id: "red", hex: "#e11d48", label: "Red" },
        { id: "orange", hex: "#f97316", label: "Orange" },
        { id: "yellow", hex: "#eab308", label: "Yellow" },
        { id: "lime", hex: "#84cc16", label: "Lime" },
        { id: "green", hex: "#22c55e", label: "Green" },
        { id: "teal", hex: "#14b8a6", label: "Teal" },
        { id: "cyan", hex: "#06b6d4", label: "Cyan" },
        { id: "blue", hex: "#3b82f6", label: "Blue" },
        { id: "indigo", hex: "#6366f1", label: "Indigo" },
        { id: "purple", hex: "#a855f7", label: "Purple" },
        { id: "magenta", hex: "#d946ef", label: "Magenta" },
        { id: "pink", hex: "#ec4899", label: "Pink" },
        { id: "brown", hex: "#92400e", label: "Brown" },
        { id: "white", hex: "#e8f0ff", label: "White" },
        { id: "gray", hex: "#9aa3b2", label: "Gray" }
    ];

    const HW = [
        { reeds: [16, 17, 18, 19], power: 0 },
        { reeds: [20, 21, 22, 23], power: 1 },
        { reeds: [32, 33, 34, 35], power: 2 },
        { reeds: [36, 37, 38, 39], power: 3 },
        { reeds: [28, 29, 30, 31], power: 4 },
        { reeds: [24, 25, 26, 27], power: 5 },
        { reeds: [44, 45, 46, 47], power: 6 },
        { reeds: [40, 41, 42, 43], power: 7 }
    ];

    const MODE_KEYS = ["io", "valve_order", "target_positions"];
    const SLOT_LETTERS = "ABCDEFGH";

    function defaultValves() {
        /* Crafty Fox valve4.js: validValves, colours, targetValveOrder, RND seats. */
        return [
            { id: 0, enabled: true, name: "A-B", seats: 2, color: "yellow", order: 1, target: "rnd" },
            { id: 1, enabled: true, name: "YEL", seats: 4, color: "yellow", order: 2, target: "rnd" },
            { id: 2, enabled: true, name: "BLK", seats: 4, color: "gray", order: 6, target: "rnd" },
            { id: 3, enabled: true, name: "WHT", seats: 4, color: "white", order: 4, target: "rnd" },
            { id: 4, enabled: false, name: "---", seats: 4, color: "clear", order: 8, target: "rnd" },
            { id: 5, enabled: true, name: "BLU", seats: 4, color: "blue", order: 5, target: "rnd" },
            { id: 6, enabled: true, name: "GRN", seats: 4, color: "green", order: 7, target: "rnd" },
            { id: 7, enabled: true, name: "RED", seats: 4, color: "red", order: 3, target: "rnd" }
        ];
    }

    function normalizeSlot(letter) {
        const c = String(letter || "A").trim().toUpperCase().charAt(0);
        return SLOT_LETTERS.indexOf(c) >= 0 ? c : "A";
    }

    function cloneValves(saved) {
        return mergeValves(saved);
    }

    function defaultGameSettings() {
        return { io: "A", valve_order: "A", target_positions: "A" };
    }

    function defaultModePresets() {
        const seed = defaultValves();
        const out = {};
        MODE_KEYS.forEach((mode) => {
            out[mode] = {};
            for (let i = 0; i < SLOT_LETTERS.length; i++) {
                out[mode][SLOT_LETTERS.charAt(i)] = cloneValves(seed);
            }
        });
        return out;
    }

    function mergeGameSettings(raw) {
        const base = defaultGameSettings();
        if (!raw || typeof raw !== "object") {
            return base;
        }
        MODE_KEYS.forEach((mode) => {
            base[mode] = normalizeSlot(raw[mode] || base[mode]);
        });
        return base;
    }

    function decodeSlot(raw) {
        if (Array.isArray(raw)) {
            return cloneValves(raw);
        }
        if (typeof raw !== "string" || !raw) {
            return cloneValves(defaultValves());
        }
        return cloneValves(raw.split(";").map((row, i) => {
            const p = row.split(",");
            return {
                id: i,
                enabled: p[0] === "1",
                name: p[1] || "V",
                seats: Number(p[2]) || 4,
                color: p[3] || "clear",
                order: Number(p[4]) || (i + 1),
                target: p[5] || "rnd"
            };
        }));
    }

    function encodeSlot(valves) {
        return cloneValves(valves).map((v) => {
            const t = v.target === "rnd" ? "rnd" : String(v.target);
            return [v.enabled ? 1 : 0, v.name, v.seats, v.color, v.order, t].join(",");
        }).join(";");
    }

    function encodeModePresets(presets) {
        const out = {};
        MODE_KEYS.forEach((mode) => {
            out[mode] = {};
            for (let i = 0; i < SLOT_LETTERS.length; i++) {
                const letter = SLOT_LETTERS.charAt(i);
                out[mode][letter] = encodeSlot(presets[mode][letter]);
            }
        });
        return out;
    }

    function mergeModePresets(raw) {
        const base = defaultModePresets();
        if (!raw || typeof raw !== "object") {
            return base;
        }
        MODE_KEYS.forEach((mode) => {
            const src = raw[mode];
            if (!src || typeof src !== "object") {
                return;
            }
            for (let i = 0; i < SLOT_LETTERS.length; i++) {
                const letter = SLOT_LETTERS.charAt(i);
                if (src[letter]) {
                    base[mode][letter] = decodeSlot(src[letter]);
                }
            }
        });
        return base;
    }

    const DEFAULT_CONFIG = {
        gameMode: "io",
        gameSetting: "A",
        gameSettings: defaultGameSettings(),
        modePresets: defaultModePresets(),
        scanPeriodMs: 10,
        debounceCount: 5,
        settleMs: 50,
        heartbeatInterval: 10000,
        debug: true,
        valves: defaultValves()
    };

    function el(id) {
        return document.getElementById(id);
    }

    function nowIso() {
        return new Date().toISOString();
    }

    function isServedOverHttp() {
        return window.location.protocol === "http:" || window.location.protocol === "https:";
    }

    function getApiBase() {
        if (isServedOverHttp()) {
            return window.location.origin;
        }
        return localStorage.getItem(KEY_BASE) || "http://192.168.4.1";
    }

    function queryParam(name) {
        try {
            return new URLSearchParams(window.location.search).get(name);
        } catch {
            return null;
        }
    }

    function queryDemoOverride() {
        const q = queryParam("demo");
        if (q === "1" || q === "true") {
            return true;
        }
        if (q === "0" || q === "false") {
            return false;
        }
        return null;
    }

    function isLocalPreviewHost() {
        const h = window.location.hostname;
        return h === "127.0.0.1" || h === "localhost" || h === "[::1]";
    }

    function getDemoMode() {
        const q = queryDemoOverride();
        if (q !== null) {
            return q;
        }
        if (isLocalPreviewHost()) {
            return true;
        }
        return localStorage.getItem(KEY_DEMO) === "1";
    }

    const SCENARIOS = ["nominal", "disabled", "ab-off", "ab-a", "ab-b", "path-mid", "path-rewind", "solved", "targets"];

    function getScenario() {
        const s = (queryParam("scenario") || "nominal").toLowerCase();
        return SCENARIOS.indexOf(s) >= 0 ? s : "nominal";
    }

    function normalizeApiPath(path) {
        return path.startsWith("/") ? path.slice(1) : path;
    }

    function resolveApiUrl(path) {
        const rel = normalizeApiPath(path);
        if (!isServedOverHttp()) {
            return getApiBase().replace(/\/$/, "") + "/" + rel;
        }
        try {
            return new URL(rel, document.baseURI || window.location.href).href;
        } catch {
            return "/" + rel;
        }
    }

    function normalizeMode(mode) {
        if (mode === "fixed_sequence") {
            return "valve_order";
        }
        if (mode === "io" || mode === "valve_order" || mode === "target_positions") {
            return mode;
        }
        return "io";
    }

    function palette(id) {
        return PALETTE.find((p) => p.id === id) || PALETTE[0];
    }

    function mergeValves(saved) {
        const base = defaultValves();
        if (!Array.isArray(saved)) {
            return base;
        }
        return base.map((row) => {
            const hit = saved.find((v) => Number(v.id) === row.id) || {};
            return {
                id: row.id,
                enabled: hit.enabled != null ? Boolean(hit.enabled) : row.enabled,
                name: String(hit.name != null ? hit.name : row.name).slice(0, 6),
                seats: [2, 3, 4].indexOf(Number(hit.seats)) >= 0 ? Number(hit.seats) : row.seats,
                color: palette(hit.color).id,
                order: Number(hit.order) || row.order,
                target: normalizeTargetValue(hit.target != null ? hit.target : row.target)
            };
        });
    }

    function loadDemoConfig() {
        let raw = {};
        try {
            raw = JSON.parse(localStorage.getItem(KEY_CFG) || "{}");
        } catch {
            raw = {};
        }
        const gameMode = normalizeMode(raw.gameMode || DEFAULT_CONFIG.gameMode);
        const gameSettings = mergeGameSettings(raw.gameSettings);
        const modePresets = mergeModePresets(raw.modePresets);
        const gameSetting = normalizeSlot(raw.gameSetting || gameSettings[gameMode] || "A");
        gameSettings[gameMode] = gameSetting;
        if (raw.modePresets && raw.valves) {
            modePresets[gameMode][gameSetting] = mergeValves(raw.valves);
        }
        return {
            gameMode: gameMode,
            gameSetting: gameSetting,
            gameSettings: gameSettings,
            modePresets: modePresets,
            scanPeriodMs: Number(raw.scanPeriodMs) || DEFAULT_CONFIG.scanPeriodMs,
            debounceCount: Number(raw.debounceCount) || DEFAULT_CONFIG.debounceCount,
            settleMs: Number(raw.settleMs) || DEFAULT_CONFIG.settleMs,
            heartbeatInterval: Number(raw.heartbeatInterval) || DEFAULT_CONFIG.heartbeatInterval,
            debug: raw.debug != null ? Boolean(raw.debug) : true,
            valves: cloneValves(modePresets[gameMode][gameSetting])
        };
    }

    function saveDemoConfig(cfg) {
        demoConfig = {
            gameMode: normalizeMode(cfg.gameMode),
            gameSetting: normalizeSlot(cfg.gameSetting || (cfg.gameSettings && cfg.gameSettings[normalizeMode(cfg.gameMode)]) || "A"),
            gameSettings: mergeGameSettings(cfg.gameSettings),
            modePresets: mergeModePresets(cfg.modePresets),
            scanPeriodMs: Number(cfg.scanPeriodMs) || 10,
            debounceCount: Number(cfg.debounceCount) || 5,
            settleMs: Number(cfg.settleMs) || 50,
            heartbeatInterval: Number(cfg.heartbeatInterval) || 10000,
            debug: Boolean(cfg.debug),
            valves: mergeValves(cfg.valves)
        };
        const mode = demoConfig.gameMode;
        const slot = demoConfig.gameSetting;
        demoConfig.gameSettings[mode] = slot;
        if (cfg.valves) {
            demoConfig.modePresets[mode][slot] = cloneValves(cfg.valves);
        }
        localStorage.setItem(KEY_CFG, JSON.stringify(demoConfig));
    }

    let demoConfig = loadDemoConfig();

    function seatDefs(valve) {
        if (valve.seats === 2) {
            return [
                { pos: 0, label: "OFF", slot: "n", off: true },
                { pos: 1, label: "A", slot: "w" },
                { pos: 2, label: "B", slot: "e" }
            ];
        }
        if (valve.seats === 3) {
            return [
                { pos: 1, label: "L", slot: "w" },
                { pos: 2, label: "T", slot: "n" },
                { pos: 3, label: "R", slot: "e" }
            ];
        }
        return [
            { pos: 1, label: "N", slot: "n" },
            { pos: 4, label: "W", slot: "w" },
            { pos: 2, label: "E", slot: "e" },
            { pos: 3, label: "S", slot: "s" }
        ];
    }

    function seatLabel(valve, pos) {
        const hit = seatDefs(valve).find((s) => s.pos === pos);
        return hit ? hit.label : "OFF";
    }

    function normalizeTargetValue(t) {
        if (t === "rnd" || t === "RND" || t === -1 || t === "-1") {
            return "rnd";
        }
        const n = Number(t);
        if (n >= 1 && n <= 4) {
            return n;
        }
        return "rnd";
    }

    function legalSeats(valve) {
        return seatDefs(valve).filter((s) => !s.off);
    }

    function pickRndTarget(valve, position) {
        const seats = legalSeats(valve);
        const others = seats.filter((s) => s.pos !== position);
        const pool = others.length ? others : seats;
        return pool[0] ? pool[0].pos : 0;
    }

    function displayIds(mode, valves) {
        if (mode === "io") {
            return valves.map((v) => v.id);
        }
        const live = valves.filter((v) => v.enabled);
        if (mode === "valve_order") {
            return live.slice().sort((a, b) => a.order - b.order || a.id - b.id).map((v) => v.id);
        }
        return live.slice().sort((a, b) => a.id - b.id).map((v) => v.id);
    }

    function modeLabel(mode) {
        if (mode === "valve_order") {
            return "Valve Order";
        }
        if (mode === "target_positions") {
            return "Target Positions";
        }
        return "Basic I/O";
    }

    function ownsPuzzle(mode) {
        return mode === "valve_order" || mode === "target_positions";
    }

    function pinRole(logical) {
        for (let i = 0; i < HW.length; i++) {
            if (HW[i].power === logical) {
                return "pwr" + i;
            }
            const ri = HW[i].reeds.indexOf(logical);
            if (ri >= 0) {
                return "v" + i + " r" + (ri + 1);
            }
        }
        return "—";
    }

    function reedBits(position, inlet, seats) {
        const bits = [1, 1, 1, 1];
        if (position >= 1 && position <= seats) {
            bits[position - 1] = 0;
        }
        if (inlet >= 1 && inlet <= 4) {
            bits[inlet - 1] = 0;
        }
        return bits;
    }

    function buildPins(valves) {
        const pins = [];
        for (let p = 0; p < 48; p++) {
            pins.push({ logical: p, chip: Math.floor(p / 16), role: pinRole(p), mode: "in", level: 1 });
        }
        valves.forEach((v) => {
            const hw = HW[v.id];
            pins[hw.power].mode = "out";
            pins[hw.power].level = v.inlet > 0 ? 1 : 0;
            v.reeds.forEach((bit, i) => {
                const pin = hw.reeds[i];
                pins[pin].mode = v.inlet === i + 1 ? "out" : "in";
                pins[pin].level = bit;
            });
        });
        return pins;
    }

    function circleFill(colorId) {
        const hex = palette(colorId).hex;
        if (!hex) {
            return "transparent";
        }
        const n = hex.replace("#", "");
        const r = parseInt(n.slice(0, 2), 16);
        const g = parseInt(n.slice(2, 4), 16);
        const b = parseInt(n.slice(4, 6), 16);
        return "rgba(" + r + "," + g + "," + b + ",0.38)";
    }

    function enrich(positions, inlets, defs, mode) {
        return defs.map((meta) => {
            const position = positions[meta.id];
            const inlet = inlets[meta.id];
            const target = ownsPuzzle(mode) && meta.enabled ? normalizeTargetValue(meta.target) : 0;
            const rnd = target === "rnd";
            return {
                id: meta.id,
                enabled: meta.enabled,
                name: meta.name,
                seats: meta.seats,
                color: meta.color,
                order: meta.order,
                position: position,
                inlet: inlet,
                target: target,
                resolvedTarget: rnd ? 0 : (Number(target) || 0),
                power: inlet > 0,
                reeds: reedBits(position, inlet, meta.seats),
                posLabel: seatLabel(meta, position),
                targetLabel: rnd ? "RND" : (target ? seatLabel(meta, target) : "—"),
                phase: "idle"
            };
        });
    }

    let rndPicks = {};
    let lastScenarioName = "";

    function applyPuzzlePhases(valves, mode, scenarioName) {
        if (scenarioName !== lastScenarioName) {
            rndPicks = {};
            lastScenarioName = scenarioName;
        }

        valves.forEach((v) => {
            v.phase = "idle";
            if (v.target === "rnd") {
                v.resolvedTarget = 0;
                v.targetLabel = "RND";
            } else {
                v.resolvedTarget = Number(v.target) || 0;
                v.targetLabel = v.resolvedTarget ? seatLabel(v, v.resolvedTarget) : "RND";
            }
        });

        function resolveRnd(v) {
            if (v.target !== "rnd") {
                return;
            }
            if (rndPicks[v.id] == null) {
                rndPicks[v.id] = pickRndTarget(v, v.position);
            }
            v.resolvedTarget = rndPicks[v.id];
            v.targetLabel = seatLabel(v, v.resolvedTarget);
        }

        if (mode === "io") {
            valves.forEach((v) => {
                v.phase = "on";
            });
            return;
        }

        if (mode === "target_positions") {
            valves.forEach((v) => {
                if (!v.enabled) {
                    return;
                }
                v.phase = "active";
                resolveRnd(v);
            });
            return;
        }

        if (mode !== "valve_order") {
            return;
        }

        let blocked = false;
        displayIds(mode, valves).forEach((id) => {
            const v = valves.find((x) => x.id === id);
            if (!v) {
                return;
            }
            if (blocked) {
                v.phase = "waiting";
                delete rndPicks[v.id];
                if (v.target === "rnd") {
                    v.resolvedTarget = 0;
                    v.targetLabel = "RND";
                }
                return;
            }
            resolveRnd(v);
            if (v.resolvedTarget && v.position === v.resolvedTarget) {
                v.phase = "done";
            } else {
                v.phase = "active";
                blocked = true;
            }
        });
    }

    function solutionString(valves, mode) {
        if (!ownsPuzzle(mode)) {
            return "";
        }
        return displayIds(mode, valves).map((id) => {
            const v = valves.find((x) => x.id === id);
            return v.name + "@" + v.targetLabel;
        }).join(" ");
    }

    function scenarioState(name) {
        const now = Date.now();
        const defs = demoConfig.valves.map((v) => Object.assign({}, v));
        let positions = [1, 2, 4, 3, 0, 1, 2, 3];
        let inlets = [3, 4, 0, 0, 0, 0, 0, 1];
        let enabled = true;
        let gameMode = demoConfig.gameMode;
        let puzzleState = "running";
        let solved = false;

        if (name === "disabled") {
            gameMode = "io";
            enabled = false;
            inlets = [0, 0, 0, 0, 0, 0, 0, 0];
            puzzleState = "paused";
        } else if (name === "ab-off") {
            gameMode = "io";
            positions[0] = 0;
            inlets[0] = 0;
        } else if (name === "ab-a") {
            gameMode = "valve_order";
            positions[0] = 1;
            inlets[0] = 3;
            defs[0].target = 2;
        } else if (name === "ab-b") {
            gameMode = "valve_order";
            positions[0] = 2;
            inlets[0] = 3;
            defs[0].target = 1;
        } else if (name === "path-mid") {
            gameMode = "valve_order";
            positions = [2, 3, 1, 4, 2, 2, 3, 1];
            defs[0].target = 2;
            defs[1].target = 3;
            defs[7].target = 1;
            defs[3].target = 4;
            inlets = [3, 4, 0, 3, 0, 0, 0, 2];
        } else if (name === "path-rewind") {
            gameMode = "valve_order";
            positions = [1, 3, 4, 4, 0, 1, 4, 1];
            defs[0].target = 2;
            defs[1].target = 3;
            defs[7].target = 1;
            defs[3].target = 4;
            defs[5].target = 1;
            defs[2].target = 4;
            defs[6].target = 4;
            inlets = [3, 0, 0, 0, 0, 0, 0, 0];
        } else if (name === "solved") {
            gameMode = "valve_order";
            solved = true;
            puzzleState = "solved";
            positions = [2, 3, 4, 2, 0, 1, 4, 1];
            defs[0].target = 2;
            defs[1].target = 3;
            defs[2].target = 4;
            defs[3].target = 2;
            defs[5].target = 1;
            defs[6].target = 4;
            defs[7].target = 1;
            inlets = [3, 2, 1, 4, 0, 3, 2, 4];
        } else if (name === "targets") {
            gameMode = "target_positions";
            positions = [1, 4, 2, 2, 0, 3, 1, 2];
            defs[0].target = 2;
            defs[1].target = 4;
            defs[3].target = 2;
            defs[5].target = 1;
        }

        if (name === "nominal") {
            gameMode = "io";
        }

        const valves = enrich(positions, inlets, defs, gameMode);
        applyPuzzlePhases(valves, gameMode, name);
        return {
            ts: now,
            id: "Valve32Prop",
            status: "online",
            scenario: name,
            version: "0.01-demo",
            enabled: enabled,
            gameMode: gameMode,
            puzzleState: puzzleState,
            solved: solved,
            solution: solutionString(valves, gameMode),
            valves: valves,
            pins: buildPins(valves),
            wifiConnected: true,
            wifiSsid: "Paradox-StageAP",
            wifiRssi: -47
        };
    }

    let demoCache = scenarioState(getScenario());
    let demoEnabledOverride = null;

    function applyEnabled(state, on) {
        state.enabled = on;
        if (!on) {
            state.valves.forEach((v) => {
                v.inlet = 0;
                v.power = false;
                v.reeds = reedBits(v.position, 0, v.seats);
            });
            state.pins = buildPins(state.valves);
            state.puzzleState = "paused";
        }
        return state;
    }

    async function api(path, options) {
        if (getDemoMode()) {
            return mockResponse(path, options);
        }
        const requestUrl = resolveApiUrl(path);
        const res = await fetch(requestUrl, options);
        const rawText = await res.text();
        let data = null;
        if (rawText) {
            try {
                data = JSON.parse(rawText);
            } catch {
                data = null;
            }
        }
        if (!res.ok) {
            throw new Error("HTTP " + res.status + " " + res.statusText);
        }
        return data;
    }

    async function mockResponse(path, options) {
        await new Promise((r) => setTimeout(r, 60));
        const payload = options && options.body ? JSON.parse(options.body) : {};
        const cmd = payload.Command || payload.command;

        if (path === "/api/state" || path === "/api/monitor") {
            demoCache = scenarioState(getScenario());
            if (demoEnabledOverride !== null) {
                applyEnabled(demoCache, demoEnabledOverride);
            }
            return demoCache;
        }

        if (path === "/api/config/defaults") {
            return JSON.parse(JSON.stringify(DEFAULT_CONFIG));
        }
        if (path === "/api/config" && options && options.method === "POST") {
            saveDemoConfig(payload);
            return { ok: true, mock: true, gameMode: demoConfig.gameMode };
        }
        if (path === "/api/config") {
            return JSON.parse(JSON.stringify(demoConfig));
        }
        if (path === "/api/config/save") {
            saveDemoConfig(payload);
            return { ok: true, mock: true, persisted: true, gameMode: demoConfig.gameMode };
        }
        if (path === "/api/config/restore" || path === "/api/config/restore/save") {
            saveDemoConfig(DEFAULT_CONFIG);
            return { ok: true, mock: true, gameMode: demoConfig.gameMode };
        }

        if (path === "/api/command") {
            if (cmd === "disable") {
                demoEnabledOverride = false;
                applyEnabled(demoCache, false);
            } else if (cmd === "enable" || cmd === "forceScan" || cmd === "start") {
                demoEnabledOverride = true;
                applyEnabled(demoCache, true);
            } else if (cmd === "reset") {
                demoEnabledOverride = false;
                applyEnabled(demoCache, false);
            } else if (payload.Valve != null && payload.Inlet != null) {
                const id = Number(payload.Valve);
                const inlet = Number(payload.Inlet);
                if (demoCache.valves[id]) {
                    demoCache.valves[id].inlet = inlet;
                    demoCache.valves[id].power = inlet > 0;
                    demoCache.valves[id].reeds = reedBits(demoCache.valves[id].position, inlet, demoCache.valves[id].seats);
                    demoCache.pins = buildPins(demoCache.valves);
                }
            }
            return { ok: true, mock: true, Command: cmd || payload };
        }

        if (path === "/api/connection") {
            if (options && options.method === "POST") {
                return { ok: true, mock: true, applied: true };
            }
            return {
                wifiSsid: "Paradox-StageAP",
                wifiPassword: "",
                mqttHost: "192.168.8.130",
                mqttPort: 1883,
                mqttUsername: "",
                mqttPassword: "",
                mqttBaseTopic: "/Paradox/TFD/Valve/Prop",
                mqttCommandTopic: "/Paradox/TFD/Valve/Prop/Commands",
                mqttStateTopic: "/Paradox/TFD/Valve/Prop/state",
                mqttEventsTopic: "/Paradox/TFD/Valve/Prop/Events",
                mqttWarningsTopic: "/Paradox/TFD/Valve/Prop/warnings",
                mqttGameStateTopic: "paradox/tfd/state",
                mqttPropAnnounceTopic: "/Paradox/Props",
                networkName: "px-valve-v1-a1b2",
                apSsid: "Paradox-PXValveV1-A1B2",
                apIpAddress: "192.168.4.1",
                apPassword: "",
                apEnabled: true
            };
        }

        if (path === "/api/connection/scan") {
            return {
                ok: true,
                networks: [
                    { ssid: "Paradox-TFD-1", rssi: -40 },
                    { ssid: "Props-Backstage", rssi: -58 },
                    { ssid: "TMOBILE", rssi: -64 }
                ]
            };
        }

        if (path === "/api/details") {
            return {
                propName: "Valve32Prop",
                ipAddress: "192.168.8.50",
                softwareVersion: "0.01-demo",
                buildNumber: "demo",
                buildDate: "2026-09-03",
                cpuTemp: "39 C",
                freeMemory: "176 KB"
            };
        }

        return { ok: true, mock: true };
    }

    function appendLog(node, value) {
        if (!node) {
            return;
        }
        node.textContent = "[" + nowIso() + "]\n" + JSON.stringify(value, null, 2) + "\n\n" + node.textContent;
    }

    function tablerWifiSvg(level) {
        const l = Math.max(0, Math.min(4, Number(level || 0)));
        const color = l >= 3 ? "#00c45c" : l === 2 ? "#f0a92a" : "#ef4444";
        const op1 = l >= 1 ? 1 : 0.25;
        const op2 = l >= 2 ? 1 : 0.25;
        const op3 = l >= 3 ? 1 : 0.25;
        const op4 = l >= 4 ? 1 : 0.25;
        return `<svg class="wifi-svg" viewBox="0 0 24 24" aria-hidden="true"><path d="M3 9.5a13 13 0 0 1 18 0" fill="none" stroke="${color}" stroke-opacity="${op4}" stroke-width="1.8" stroke-linecap="round"/><path d="M6 13a9 9 0 0 1 12 0" fill="none" stroke="${color}" stroke-opacity="${op3}" stroke-width="1.8" stroke-linecap="round"/><path d="M9 16.5a5 5 0 0 1 6 0" fill="none" stroke="${color}" stroke-opacity="${op2}" stroke-width="1.8" stroke-linecap="round"/><circle cx="12" cy="20" r="1.5" fill="${color}" fill-opacity="${op1}"/></svg>`;
    }

    function rssiLevel(rssi) {
        if (rssi >= -50) {
            return 4;
        }
        if (rssi >= -60) {
            return 3;
        }
        if (rssi >= -70) {
            return 2;
        }
        return 1;
    }

    function ensureWifiBadge() {
        let badge = el("wifiBadge");
        if (badge) {
            return badge;
        }
        const container = el("statusIcons");
        if (!container) {
            return null;
        }
        badge = document.createElement("div");
        badge.id = "wifiBadge";
        badge.className = "wifi-badge";
        badge.innerHTML = `<span id="wifiBadgeIcon" class="wifi-icon"></span><span id="wifiBadgeText">--</span>`;
        container.insertBefore(badge, container.firstChild);
        return badge;
    }

    function renderWifiStatus(details) {
        const badge = ensureWifiBadge();
        if (!badge) {
            return;
        }
        const icon = el("wifiBadgeIcon");
        const text = el("wifiBadgeText");
        if (details && details.wifiConnected) {
            if (icon) {
                icon.innerHTML = tablerWifiSvg(rssiLevel(details.wifiRssi));
            }
            if (text) {
                text.textContent = details.wifiSsid || "WiFi";
            }
        } else if (icon && text) {
            icon.innerHTML = tablerWifiSvg(0);
            text.textContent = "off";
        }
    }

    async function fetchStatusIcons() {
        try {
            renderWifiStatus(await api("/api/state"));
        } catch {
            renderWifiStatus(null);
        }
    }

    function seatClass(valve, seat) {
        const cls = ["seat", seat.slot];
        if (seat.off) {
            cls.push("off");
        }
        const isCurrent = valve.position === seat.pos;
        const resolved = valve.resolvedTarget;
        const isTarget = ownsPuzzle(demoCache.gameMode) && resolved && resolved === seat.pos && !seat.off;
        const waiting = valve.phase === "waiting";
        if (waiting) {
            if (isTarget) {
                cls.push("target");
            } else if (isCurrent) {
                cls.push("current");
            }
        } else if (isCurrent && isTarget) {
            cls.push("solved");
        } else if (isTarget) {
            cls.push("target");
        } else if (isCurrent) {
            cls.push("current");
        }
        return cls.join(" ");
    }

    function renderGraphic(valve) {
        const seats = seatDefs(valve);
        const cells = ["n", "w", "hub", "e", "s"].map((slot) => {
            const seat = seats.find((s) => s.slot === slot);
            if (slot === "hub") {
                return `<div class="seat hub"></div>`;
            }
            if (!seat) {
                return `<div class="seat ${slot}" style="visibility:hidden"></div>`;
            }
            return `<div class="${seatClass(valve, seat)}">${seat.label}</div>`;
        });
        return `<div class="valve-graphic" style="--valve-fill:${circleFill(valve.color)}">${cells.join("")}</div>`;
    }

    function renderStation(valve) {
        const phase = valve.phase && valve.phase !== "idle" ? " phase-" + valve.phase : "";
        const rnd = valve.target === "rnd" ? `<span class="rnd-tag">RND</span>` : "";
        return `<article class="valve-station${phase}">
            <div class="valve-name">${valve.name}${rnd}</div>
            ${renderGraphic(valve)}
            <span class="valve-id">${valve.id}</span>
        </article>`;
    }

    function setLive(state) {
        demoCache = state;
        const panel = el("gameStatePanel");
        const badge = el("stateBadge");
        const modeBadge = el("modeBadge");
        const hint = el("modeHint");
        const sol = el("solutionLine");
        const path = el("valvePath");
        const puzzle = el("puzzleActions");
        const mode = state.gameMode;

        if (modeBadge) {
            modeBadge.textContent = modeLabel(mode);
            modeBadge.className = "badge " + (mode === "io" ? "mode-io" : mode === "target_positions" ? "mode-targets" : "mode-order");
        }
        if (badge) {
            badge.textContent = state.enabled ? (state.solved ? "solved" : state.puzzleState) : "disabled";
            badge.className = "badge " + (state.solved ? "state-solved" : state.enabled ? "state-running" : "state-paused");
        }
        if (panel) {
            panel.className = "panel pulse " + (state.solved ? "state-solved" : state.enabled ? "state-running" : "state-paused");
        }
        if (hint) {
            hint.textContent = mode === "io"
                ? "Basic I/O — Node valve3.js owns the path. This prop reports positions and drives inlets."
                : mode === "valve_order"
                    ? "Valve Order — sequential path. Dim cards are waiting; only the bright one is active. Stop valve3.js on .130."
                    : "Target Positions — every enabled valve to its target, any order. Stop valve3.js on .130.";
        }
        if (sol) {
            if (ownsPuzzle(mode) && state.solution) {
                sol.textContent = state.solution;
                sol.classList.remove("hidden");
            } else {
                sol.classList.add("hidden");
            }
        }
        if (puzzle) {
            puzzle.classList.toggle("hidden", !ownsPuzzle(mode));
        }
        if (path) {
            const byId = {};
            state.valves.forEach((v) => { byId[v.id] = v; });
            applyPuzzlePhases(state.valves, mode, state.scenario || "live");
            path.innerHTML = displayIds(mode, state.valves).map((id) => renderStation(byId[id])).join("");
        }
    }

    function renderValveTable(state) {
        const tbody = document.querySelector("#valveTable tbody");
        if (!tbody) {
            return;
        }
        tbody.innerHTML = state.valves.map((v) => {
            const reeds = v.reeds.map((bit, i) => {
                const driven = v.inlet === i + 1;
                return `<span class="reed-bit${bit === 0 ? " low" : ""}${driven ? " driven" : ""}" title="reed ${i + 1}">${bit}</span>`;
            }).join("");
            return `<tr>
                <td>${v.id}</td>
                <td><span class="valve-swatch" style="background:${palette(v.color).hex || "transparent"}"></span> ${v.name}${v.enabled ? "" : " · off"}</td>
                <td>${v.posLabel} (${v.position})</td>
                <td>${v.inlet}</td>
                <td class="${v.power ? "text-ok" : "text-muted"}">${v.power ? "HIGH" : "LOW"}</td>
                <td><span class="reed-bits">${reeds}</span></td>
                <td>${ownsPuzzle(state.gameMode) ? v.targetLabel : "—"}</td>
            </tr>`;
        }).join("");
    }

    function renderMcp(state) {
        const host = el("mcpGrid");
        if (!host) {
            return;
        }
        host.innerHTML = [0, 1, 2].map((chip) => {
            const rows = state.pins.filter((p) => p.chip === chip).map((p) => {
                return `<tr>
                    <td>${p.logical}</td>
                    <td>${p.role}</td>
                    <td class="${p.mode === "out" ? "pin-out" : ""}">${p.mode}</td>
                    <td class="${p.level === 0 ? "lvl-low" : "lvl-high"}">${p.level === 0 ? "LOW" : "HIGH"}</td>
                </tr>`;
            }).join("");
            return `<div class="mcp-chip">
                <h3>MCP addr ${chip} · pins ${chip * 16}–${chip * 16 + 15}</h3>
                <table class="pin-table">
                    <thead><tr><th>Pin</th><th>Role</th><th>Mode</th><th>Lvl</th></tr></thead>
                    <tbody>${rows}</tbody>
                </table>
            </div>`;
        }).join("");
    }

    async function pageDashboard() {
        async function refresh() {
            const state = await api("/api/state");
            setLive(state);
            appendLog(el("actionLog"), {
                event: "state",
                mode: state.gameMode,
                enabled: state.enabled,
                ab: state.valves[0].posLabel
            });
        }
        if (el("refreshBtn")) {
            el("refreshBtn").addEventListener("click", () => refresh().catch((e) => appendLog(el("actionLog"), String(e))));
        }
        document.querySelectorAll("[data-cmd]").forEach((btn) => {
            btn.addEventListener("click", async () => {
                try {
                    appendLog(el("actionLog"), await api("/api/command", { method: "POST", body: btn.getAttribute("data-cmd") }));
                    await refresh();
                } catch (e) {
                    appendLog(el("actionLog"), String(e));
                }
            });
        });
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh().catch((e) => appendLog(el("actionLog"), String(e)));
        setInterval(() => refresh().catch(() => {}), 1600);
    }

    async function pageMonitor() {
        async function refresh() {
            const state = await api("/api/monitor");
            if (el("monitorScenario")) {
                el("monitorScenario").textContent = (state.scenario || "live") + " · " + modeLabel(state.gameMode);
            }
            renderValveTable(state);
            renderMcp(state);
            const dump = el("monitorLog");
            if (dump && demoConfig.debug) {
                dump.classList.remove("hidden");
                dump.textContent = JSON.stringify(state.valves.map((v) => ({
                    id: v.id, position: v.position, inlet: v.inlet, reeds: v.reeds
                })), null, 2);
            }
        }
        if (el("refreshMonitor")) {
            el("refreshMonitor").addEventListener("click", () => refresh().catch(() => {}));
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh().catch(() => {});
        setInterval(() => refresh().catch(() => {}), 800);
    }

    function targetOptions(valve) {
        const t = normalizeTargetValue(valve.target);
        const seats = legalSeats(valve);
        const valid = t === "rnd" || seats.some((s) => s.pos === t);
        const use = valid ? t : "rnd";
        const opts = [`<option value="rnd"${use === "rnd" ? " selected" : ""}>RND</option>`];
        seats.forEach((s) => {
            opts.push(`<option value="${s.pos}"${use === s.pos ? " selected" : ""}>${s.label}</option>`);
        });
        return opts.join("");
    }

    function colorOptions(selected) {
        return PALETTE.map((p) => `<option value="${p.id}"${p.id === selected ? " selected" : ""}>${p.label}</option>`).join("");
    }

    function syncValveTableMode() {
        const table = el("valveConfigTable");
        const mode = el("gameMode") ? el("gameMode").value : "io";
        if (!table) {
            return;
        }
        table.classList.toggle("show-order", mode === "valve_order");
        table.classList.toggle("show-target", ownsPuzzle(mode));
    }

    function fillValveTable(valves) {
        const tbody = document.querySelector("#valveConfigTable tbody");
        if (!tbody) {
            return;
        }
        tbody.innerHTML = valves.map((v) => {
            return `<tr data-id="${v.id}">
                <td>${v.id}</td>
                <td><input type="checkbox" class="v-on" ${v.enabled ? "checked" : ""}></td>
                <td><input type="text" class="v-name" maxlength="6" value="${v.name}"></td>
                <td><select class="v-seats">
                    <option value="2"${v.seats === 2 ? " selected" : ""}>2 (L/R)</option>
                    <option value="3"${v.seats === 3 ? " selected" : ""}>3 (L/T/R)</option>
                    <option value="4"${v.seats === 4 ? " selected" : ""}>4 (L/T/R/B)</option>
                </select></td>
                <td><select class="v-color">${colorOptions(v.color)}</select></td>
                <td class="col-order"><input type="number" class="v-order" min="1" max="8" value="${v.order}"></td>
                <td class="col-target"><select class="v-target">${targetOptions(v)}</select></td>
            </tr>`;
        }).join("");
        tbody.querySelectorAll(".v-seats").forEach((sel) => {
            sel.addEventListener("change", () => {
                const row = sel.closest("tr");
                const seats = Number(sel.value);
                const prev = row.querySelector(".v-target").value;
                const targetSel = row.querySelector(".v-target");
                targetSel.innerHTML = targetOptions({ seats: seats, target: prev });
            });
        });
        tbody.querySelectorAll(".v-order").forEach((input) => {
            input.addEventListener("input", markOrderDupes);
            input.addEventListener("change", markOrderDupes);
        });
        markOrderDupes();
        syncValveTableMode();
    }

    function markOrderDupes() {
        const rows = Array.from(document.querySelectorAll("#valveConfigTable tbody tr"));
        const counts = {};
        rows.forEach((row) => {
            const n = Number(row.querySelector(".v-order").value);
            counts[n] = (counts[n] || 0) + 1;
        });
        rows.forEach((row) => {
            const input = row.querySelector(".v-order");
            const dup = counts[Number(input.value)] > 1;
            input.classList.toggle("order-dup", dup);
            row.classList.toggle("order-dup", dup);
        });
    }

    function collectValves() {
        return Array.from(document.querySelectorAll("#valveConfigTable tbody tr")).map((row) => ({
            id: Number(row.getAttribute("data-id")),
            enabled: row.querySelector(".v-on").checked,
            name: row.querySelector(".v-name").value.trim().slice(0, 6) || "V",
            seats: Number(row.querySelector(".v-seats").value),
            color: row.querySelector(".v-color").value,
            order: Number(row.querySelector(".v-order").value) || 1,
            target: normalizeTargetValue(row.querySelector(".v-target").value)
        }));
    }

    function fillForm(form, cfg) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                field.checked = Boolean(cfg[field.name]);
            } else if (cfg[field.name] != null) {
                field.value = cfg[field.name];
            }
        });
    }

    function collectForm(form, body) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                body[field.name] = field.checked;
            } else if (field.type === "number") {
                body[field.name] = Number(field.value);
            } else {
                body[field.name] = field.value;
            }
        });
    }

    function settingHint(letter) {
        if (letter === "A") {
            return "A — Crafty Fox valve4.js: order 0→1→7→3→5→2→6 (#4 off), names A-B/YEL/BLK/WHT/BLU/GRN/RED, RND targets, A–B flip.";
        }
        return letter + " — custom setting for this mode. Edit the table, then Apply or Apply + Save.";
    }

    async function pageConfig() {
        const form = el("configForm");
        const log = el("configLog");
        let pagePresets = defaultModePresets();
        let pageSettings = defaultGameSettings();

        function currentMode() {
            return normalizeMode(el("gameMode") ? el("gameMode").value : "io");
        }

        function currentSlot() {
            return normalizeSlot(el("gameSetting") ? el("gameSetting").value : "A");
        }

        function highlightSlots() {
            const slot = currentSlot();
            document.querySelectorAll(".setting-slot").forEach((btn) => {
                btn.classList.toggle("active", btn.getAttribute("data-slot") === slot);
            });
            if (el("gameSettingHint")) {
                el("gameSettingHint").textContent = settingHint(slot);
            }
        }

        function stashTable() {
            const mode = currentMode();
            const slot = currentSlot();
            pagePresets[mode][slot] = collectValves();
            pageSettings[mode] = slot;
        }

        function showSlot(letter, opts) {
            const skipStash = opts && opts.skipStash;
            if (!skipStash) {
                stashTable();
            }
            const mode = currentMode();
            const slot = normalizeSlot(letter);
            pageSettings[mode] = slot;
            if (el("gameSetting")) {
                el("gameSetting").value = slot;
            }
            fillValveTable(pagePresets[mode][slot]);
            highlightSlots();
        }

        function applyCfgToPage(cfg) {
            pagePresets = mergeModePresets(cfg.modePresets);
            pageSettings = mergeGameSettings(cfg.gameSettings);
            const mode = normalizeMode(cfg.gameMode);
            const slot = normalizeSlot(cfg.gameSetting || pageSettings[mode]);
            pageSettings[mode] = slot;
            if (cfg.valves) {
                pagePresets[mode][slot] = cloneValves(cfg.valves);
            }
            fillForm(form, cfg);
            if (el("gameSetting")) {
                el("gameSetting").value = slot;
            }
            fillValveTable(pagePresets[mode][slot]);
            highlightSlots();
            syncValveTableMode();
        }

        try {
            const cfg = await api("/api/config");
            applyCfgToPage(cfg);
            appendLog(log, cfg);
        } catch (e) {
            appendLog(log, String(e));
            applyCfgToPage(DEFAULT_CONFIG);
        }

        if (el("gameMode")) {
            el("gameMode").addEventListener("change", () => {
                stashTable();
                const mode = currentMode();
                showSlot(pageSettings[mode], { skipStash: true });
                syncValveTableMode();
            });
        }
        document.querySelectorAll(".setting-slot").forEach((btn) => {
            btn.addEventListener("click", () => {
                showSlot(btn.getAttribute("data-slot"));
            });
        });

        async function postConfig(path) {
            stashTable();
            const body = {};
            collectForm(form, body);
            body.gameMode = currentMode();
            body.gameSetting = currentSlot();
            body.gameSettings = pageSettings;
            body.modePresets = encodeModePresets(pagePresets);
            body.valves = collectValves();
            appendLog(log, await api(path, { method: "POST", body: JSON.stringify(body) }));
        }

        if (el("applyConfig")) {
            el("applyConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("saveConfig")) {
            el("saveConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config/save").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("restoreDefaults")) {
            el("restoreDefaults").addEventListener("click", (ev) => {
                ev.preventDefault();
                api("/api/config/defaults").then((cfg) => {
                    applyCfgToPage(cfg);
                    appendLog(log, cfg);
                }).catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("sendRaw")) {
            el("sendRaw").addEventListener("click", async () => {
                try {
                    appendLog(el("rawLog"), await api("/api/command", { method: "POST", body: el("rawCommand").value }));
                } catch (e) {
                    appendLog(el("rawLog"), String(e));
                }
            });
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
    }

    async function pageConnection() {
        const log = el("connectionLog");
        function fillTopics(conn) {
            ["mqttCommandTopic", "mqttStateTopic", "mqttEventsTopic", "mqttWarningsTopic"].forEach((id) => {
                if (el(id)) {
                    el(id).textContent = conn[id] || "";
                }
            });
        }
        try {
            const conn = await api("/api/connection");
            ["wifiSsid", "wifiPassword", "mqttHost", "mqttPort", "mqttUsername", "mqttPassword", "mqttBaseTopic", "mqttGameStateTopic", "mqttPropAnnounceTopic", "networkName", "apPassword"].forEach((id) => {
                if (el(id) && conn[id] != null) {
                    el(id).value = conn[id];
                }
            });
            if (el("apSsidDisplay") && conn.apSsid) {
                el("apSsidDisplay").value = conn.apSsid;
            }
            if (el("apEnabled")) {
                el("apEnabled").checked = Boolean(conn.apEnabled);
            }
            if (el("apIpNote") && conn.apIpAddress) {
                el("apIpNote").textContent = "AP IP Address: " + conn.apIpAddress;
            }
            fillTopics(conn);
            if (el("wifiStatus")) {
                el("wifiStatus").innerHTML = `<span class="wifi-icon">${tablerWifiSvg(4)}</span> Connected to <strong>${conn.wifiSsid}</strong>`;
            }
            appendLog(log, conn);
        } catch (e) {
            appendLog(log, String(e));
        }

        try {
            const scan = await api("/api/connection/scan");
            const list = el("ssidList");
            if (list && scan.networks) {
                list.innerHTML = "";
                scan.networks.forEach((n) => {
                    const b = document.createElement("button");
                    b.type = "button";
                    b.className = "ssid-item";
                    b.innerHTML = `<span>${n.ssid}</span><span class="ssid-meta"><span class="wifi-icon">${tablerWifiSvg(rssiLevel(n.rssi))}</span>${n.rssi} dBm</span>`;
                    b.addEventListener("click", () => {
                        if (el("wifiSsid")) {
                            el("wifiSsid").value = n.ssid;
                        }
                    });
                    list.appendChild(b);
                });
            }
        } catch {
            /* ignore */
        }

        try {
            const details = await api("/api/details");
            const map = {
                detailPropName: details.propName,
                detailIpAddress: details.ipAddress,
                detailSoftwareVersion: details.softwareVersion,
                detailBuildNumber: details.buildNumber,
                detailBuildDate: details.buildDate,
                detailCpuTemp: details.cpuTemp,
                detailFreeMemory: details.freeMemory
            };
            Object.keys(map).forEach((id) => {
                if (el(id)) {
                    el(id).textContent = map[id];
                }
            });
        } catch {
            /* ignore */
        }

        ["connectWifi", "saveConnection", "applyTopics", "applyDeviceName"].forEach((id) => {
            if (el(id)) {
                el(id).addEventListener("click", async () => {
                    try {
                        appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
                    } catch (e) {
                        appendLog(log, String(e));
                    }
                });
            }
        });
        if (el("refreshDetails")) {
            el("refreshDetails").addEventListener("click", () => location.reload());
        }
        if (el("rebootDevice")) {
            el("rebootDevice").addEventListener("click", () => appendLog(el("deviceLog"), { mock: true, reboot: "skipped in demo" }));
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
    }

    window.PX = {
        pageDashboard: pageDashboard,
        pageConfig: pageConfig,
        pageMonitor: pageMonitor,
        pageConnection: pageConnection
    };
})();
