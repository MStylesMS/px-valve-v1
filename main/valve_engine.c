#include "valve_engine.h"
#include "mcp23s17.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "Valve32Prop";

#define VALVE_COUNT 8
#define MODE_COUNT 3
#define SLOT_COUNT 8
#define EVENT_QUEUE_LEN 16
#define EVENT_JSON_MAX 96
#define PUZZLE_PUB_LEN 8
#define PUZZLE_PUB_TOPIC 96
#define PUZZLE_PUB_PAYLOAD 280
#define CONFIG_PATH "/spiffs/config.json"
#define CONFIG_FILE_MAX 32768

#define TOPIC_VALVE_EVENTS "/Paradox/TFD/Valve/Events"
#define TOPIC_WHOOSH "/Paradox/TFD/CHMB2/ImageSwitcher/Commands"

static const char *s_mode_keys[MODE_COUNT] = {
    "io", "valve_order", "target_positions",
};

typedef struct {
    int reed_start;
    int power;
} valve_hw_t;

static const valve_hw_t s_hw[VALVE_COUNT] = {
    {16, 0}, {20, 1}, {32, 2}, {36, 3}, {28, 4}, {24, 5}, {44, 6}, {40, 7},
};

typedef struct {
    bool enabled;
    char name[8];
    int seats; /* 2, 3, or 4 */
    char color[12];
    int order;
    char target[8]; /* "rnd" or "1".."4" */
} valve_meta_t;

typedef struct {
    int scan_period_ms;
    int debounce_count;
    int settle_ms;
    int heartbeat_interval_ms;
    bool debug;
    char game_mode[24];
    char selected[MODE_COUNT]; /* 'A'..'H' per mode */
    valve_meta_t slots[MODE_COUNT][SLOT_COUNT][VALVE_COUNT];
    valve_meta_t valves[VALVE_COUNT];
} valve_config_t;

typedef struct {
    int position;
    int inlet;
    int debounce;
    int pending;
} valve_live_t;

typedef struct {
    char topic[PUZZLE_PUB_TOPIC];
    char payload[PUZZLE_PUB_PAYLOAD];
} puzzle_pub_t;

typedef struct {
    valve_config_t cfg;
    valve_live_t live[VALVE_COUNT];
    bool prop_enabled;
    bool wifi_connected;
    char wifi_ssid[33];
    int wifi_rssi;
    bool spiffs_ready;
    char event_queue[EVENT_QUEUE_LEN][EVENT_JSON_MAX];
    int event_head;
    int event_tail;
    bool puzzle_running;
    bool puzzle_solved;
    char puzzle_state[16];
    char solution[192];
    int picked[VALVE_COUNT];
    int seated[VALVE_COUNT];
    int stored_inlet[VALVE_COUNT];
    bool step_on[VALVE_COUNT];
    bool gm_forced[VALVE_COUNT];
    puzzle_pub_t pubs[PUZZLE_PUB_LEN];
    int pub_head;
    int pub_tail;
    SemaphoreHandle_t lock;
} valve_ctx_t;

static valve_ctx_t s_ctx;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int clamp_int(int value, int min_v, int max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static bool valve_lock(void)
{
    if (!s_ctx.lock) {
        return true;
    }
    return xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void valve_unlock(void)
{
    if (s_ctx.lock) {
        xSemaphoreGive(s_ctx.lock);
    }
}

/* Crafty Fox valve4.js: names, enable mask, evaluation order, RND seats.
 * Order IDs [0, 1, 3, 7, 5, 2, 6, 4]; valve 4 unused; white (ID 7) off.
 * A–B is 2-seat RND (A↔B). */
static const valve_meta_t s_valve4_defs[VALVE_COUNT] = {
    {true, "A-B", 2, "yellow", 1, "rnd"},
    {true, "RED", 4, "red", 2, "rnd"},
    {true, "BLK", 4, "gray", 6, "rnd"},
    {true, "YEL", 4, "yellow", 3, "rnd"},
    {false, "---", 4, "clear", 8, "rnd"},
    {true, "BLU", 4, "blue", 5, "rnd"},
    {true, "GRN", 4, "green", 7, "rnd"},
    {true, "WHT", 4, "white", 4, "rnd"},
};

static int mode_index(const char *mode)
{
    if (mode && strcmp(mode, "valve_order") == 0) {
        return 1;
    }
    if (mode && strcmp(mode, "target_positions") == 0) {
        return 2;
    }
    return 0;
}

static int slot_index_from_char(char c)
{
    if (c >= 'a' && c <= 'h') {
        c = (char)(c - 32);
    }
    if (c >= 'A' && c <= 'H') {
        return c - 'A';
    }
    return 0;
}

static int slot_index_from_str(const char *s)
{
    return slot_index_from_char(s && s[0] ? s[0] : 'A');
}

static void sync_active_from_slot(valve_config_t *cfg)
{
    int m = mode_index(cfg->game_mode);
    int s = slot_index_from_char(cfg->selected[m]);
    memcpy(cfg->valves, cfg->slots[m][s], sizeof(cfg->valves));
}

static void sync_slot_from_active(valve_config_t *cfg)
{
    int m = mode_index(cfg->game_mode);
    int s = slot_index_from_char(cfg->selected[m]);
    memcpy(cfg->slots[m][s], cfg->valves, sizeof(cfg->valves));
}

static void set_default_config(valve_config_t *cfg)
{
    cfg->scan_period_ms = 5;
    cfg->debounce_count = 2;
    cfg->settle_ms = 50;
    cfg->heartbeat_interval_ms = 10000;
    cfg->debug = true;
    copy_bounded(cfg->game_mode, sizeof(cfg->game_mode), "io");
    for (int m = 0; m < MODE_COUNT; m++) {
        cfg->selected[m] = 'A';
        for (int s = 0; s < SLOT_COUNT; s++) {
            memcpy(cfg->slots[m][s], s_valve4_defs, sizeof(s_valve4_defs));
        }
    }
    memcpy(cfg->valves, s_valve4_defs, sizeof(s_valve4_defs));
}

static void check_puzzle_locked(void);
static void build_solution_locked(void);

static void queue_event(int valve_id, int position)
{
    int next = (s_ctx.event_head + 1) % EVENT_QUEUE_LEN;
    int inlet = s_ctx.live[valve_id].inlet;
    if (next == s_ctx.event_tail) {
        s_ctx.event_tail = (s_ctx.event_tail + 1) % EVENT_QUEUE_LEN;
    }
    snprintf(s_ctx.event_queue[s_ctx.event_head], EVENT_JSON_MAX,
             "{\"Valve\":%d,\"Position\":%d,\"Enabled\":%s,\"Inlet\":%d}",
             valve_id, position, inlet > 0 ? "true" : "false", inlet);
    s_ctx.event_head = next;
}

static const char *seat_label(const valve_meta_t *meta, int pos)
{
    if (meta->seats == 2) {
        if (pos == 1) {
            return "A";
        }
        if (pos == 2) {
            return "B";
        }
        return "OFF";
    }
    if (meta->seats == 3) {
        if (pos == 1) {
            return "L";
        }
        if (pos == 2) {
            return "T";
        }
        if (pos == 3) {
            return "R";
        }
        return "OFF";
    }
    if (pos == 1) {
        return "N";
    }
    if (pos == 2) {
        return "E";
    }
    if (pos == 3) {
        return "S";
    }
    if (pos == 4) {
        return "W";
    }
    return "OFF";
}

static void activate_valve(int id, int inlet)
{
    const valve_hw_t *hw = &s_hw[id];
    int prev;
    if (inlet < 0 || inlet > 4) {
        return;
    }
    prev = s_ctx.live[id].inlet;
    s_ctx.live[id].inlet = inlet;
    if (inlet == 0) {
        mcp23s17_set_output(hw->power, 0);
        for (int i = 0; i < 4; i++) {
            mcp23s17_set_input_pullup(hw->reed_start + i);
        }
    } else {
        mcp23s17_set_output(hw->power, 1);
        for (int p = 1; p <= 4; p++) {
            int pin = hw->reed_start - 1 + p;
            if (p == inlet) {
                mcp23s17_set_output(pin, 0);
            } else {
                mcp23s17_set_input_pullup(pin);
            }
        }
    }
    if (prev != inlet) {
        queue_event(id, s_ctx.live[id].position);
    }
}

static int read_position(int id, bool forced)
{
    const valve_hw_t *hw = &s_hw[id];
    int inlet = s_ctx.live[id].inlet;
    int pos = 0;
    int inlet_pin = (inlet >= 1 && inlet <= 4) ? (hw->reed_start - 1 + inlet) : -1;

    if (forced && inlet_pin >= 0) {
        mcp23s17_set_input_pullup(inlet_pin);
        vTaskDelay(pdMS_TO_TICKS(s_ctx.cfg.settle_ms));
    }

    for (int i = 0; i < 4; i++) {
        int pin = hw->reed_start + i;
        if (!forced && (i + 1) == inlet) {
            continue;
        }
        int level = mcp23s17_read(pin);
        if (level == 0) {
            pos = i + 1;
        }
    }

    if (forced && inlet_pin >= 0) {
        mcp23s17_set_output(inlet_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return pos;
}

static void publish_if_changed(int id, int pos, bool force)
{
    if (force || pos != s_ctx.live[id].position) {
        s_ctx.live[id].position = pos;
        if (pos == 1 || pos == 2 || (pos >= 3 && pos <= 4 && s_ctx.cfg.valves[id].seats > 2)) {
            s_ctx.seated[id] = pos;
        }
        queue_event(id, pos);
        check_puzzle_locked();
    }
}

static void force_scan_all(void)
{
    if (!mcp23s17_ready()) {
        return;
    }
    for (int i = 0; i < VALVE_COUNT; i++) {
        int pos = read_position(i, true);
        s_ctx.live[i].debounce = 0;
        s_ctx.live[i].pending = pos;
        publish_if_changed(i, pos, true);
    }
}

static void disable_prop(void)
{
    s_ctx.prop_enabled = false;
    for (int i = 0; i < VALVE_COUNT; i++) {
        activate_valve(i, 0);
    }
}

static void enable_prop(void)
{
    if (s_ctx.prop_enabled) {
        return;
    }
    force_scan_all();
    s_ctx.prop_enabled = true;
}

static bool puzzle_mode_active(void)
{
    return strcmp(s_ctx.cfg.game_mode, "valve_order") == 0 ||
           strcmp(s_ctx.cfg.game_mode, "target_positions") == 0;
}

static void queue_puzzle_pub(const char *topic, const char *payload)
{
    int next = (s_ctx.pub_head + 1) % PUZZLE_PUB_LEN;
    if (next == s_ctx.pub_tail) {
        s_ctx.pub_tail = (s_ctx.pub_tail + 1) % PUZZLE_PUB_LEN;
    }
    copy_bounded(s_ctx.pubs[s_ctx.pub_head].topic, PUZZLE_PUB_TOPIC, topic);
    copy_bounded(s_ctx.pubs[s_ctx.pub_head].payload, PUZZLE_PUB_PAYLOAD, payload);
    s_ctx.pub_head = next;
}

static void queue_whoosh(bool on)
{
    queue_puzzle_pub(TOPIC_WHOOSH,
                     on ? "{\"Command\":\"playAudioFx\",\"Audio\":\"on-whoosh.mp3\"}"
                        : "{\"Command\":\"playAudioFx\",\"Audio\":\"off-whoosh.mp3\"}");
}

static void queue_puzzle_state(const char *state)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"State\":\"%s\"}", state);
    queue_puzzle_pub(TOPIC_VALVE_EVENTS, buf);
}

static bool is_two_seat(int id)
{
    return s_ctx.cfg.valves[id].seats == 2;
}

static int seat_max(int id)
{
    int seats = s_ctx.cfg.valves[id].seats;
    if (seats == 2) {
        return 2;
    }
    if (seats == 3) {
        return 3;
    }
    return 4;
}

static void ordered_ids(int *out)
{
    for (int i = 0; i < VALVE_COUNT; i++) {
        out[i] = i;
    }
    for (int i = 0; i < VALVE_COUNT; i++) {
        for (int j = i + 1; j < VALVE_COUNT; j++) {
            int oi = s_ctx.cfg.valves[out[i]].order;
            int oj = s_ctx.cfg.valves[out[j]].order;
            if (oj < oi || (oj == oi && out[j] < out[i])) {
                int t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
        }
    }
}

static int next_enabled_index(const int *order, int after)
{
    for (int i = after + 1; i < VALVE_COUNT; i++) {
        if (s_ctx.cfg.valves[order[i]].enabled) {
            return i;
        }
    }
    return -1;
}

static int first_enabled_index(const int *order)
{
    return next_enabled_index(order, -1);
}

static int pick_ab_target(int current)
{
    /* A or OFF (or unused 3/4) → B. Only start-on-B → A. */
    if (current == 2) {
        return 1;
    }
    return 2;
}

static int pick_rnd_target(int id)
{
    int current = s_ctx.live[id].position;
    int maxs = seat_max(id);
    int pool[4];
    int n = 0;
    if (current < 1 || current > maxs) {
        current = 1; /* between seats: inlet is 1, target must not be 1 */
    }
    for (int p = 1; p <= maxs; p++) {
        if (p != current) {
            pool[n++] = p;
        }
    }
    if (n == 0) {
        for (int p = 1; p <= maxs; p++) {
            pool[n++] = p;
        }
    }
    return pool[esp_random() % (uint32_t)n];
}

static int pick_configured_target(int id)
{
    const valve_meta_t *m = &s_ctx.cfg.valves[id];
    if (strcmp(m->target, "rnd") != 0) {
        int t = atoi(m->target);
        if (t >= 1 && t <= seat_max(id)) {
            return t;
        }
    }
    return pick_rnd_target(id);
}

static void assign_step(int id)
{
    if (s_ctx.picked[id] == 0) {
        if (is_two_seat(id)) {
            s_ctx.picked[id] = pick_ab_target(s_ctx.live[id].position);
            return;
        }
        int inlet = s_ctx.live[id].position;
        if (inlet < 1 || inlet > 4) {
            inlet = 1;
        }
        s_ctx.stored_inlet[id] = inlet;
        s_ctx.picked[id] = pick_configured_target(id);
        activate_valve(id, inlet);
        return;
    }
    if (!is_two_seat(id)) {
        int inlet = s_ctx.stored_inlet[id] > 0 ? s_ctx.stored_inlet[id] : 1;
        activate_valve(id, inlet);
    }
}

static void pick_target_positions_on_enable(int id)
{
    if (s_ctx.picked[id] != 0 || !s_ctx.cfg.valves[id].enabled) {
        return;
    }
    if (is_two_seat(id)) {
        s_ctx.picked[id] = pick_ab_target(s_ctx.live[id].position);
    } else {
        int inlet = s_ctx.live[id].position;
        if (inlet < 1 || inlet > 4) {
            inlet = 1;
        }
        if (s_ctx.stored_inlet[id] == 0) {
            s_ctx.stored_inlet[id] = inlet;
        }
        s_ctx.picked[id] = pick_configured_target(id);
    }
    build_solution_locked();
}

static bool valve_correct(int id)
{
    if (!s_ctx.cfg.valves[id].enabled) {
        return true;
    }
    if (s_ctx.gm_forced[id]) {
        return true;
    }
    if (s_ctx.picked[id] == 0) {
        return false;
    }
    int pos = s_ctx.live[id].position;
    if (is_two_seat(id)) {
        return (pos == 1 || pos == 2) && pos == s_ctx.picked[id];
    }
    return pos == s_ctx.picked[id];
}

static void build_solution_locked(void)
{
    char buf[192] = "Target (Valve @ Position):";
    int order[VALVE_COUNT];
    ordered_ids(order);
    for (int i = 0; i < VALVE_COUNT; i++) {
        int id = order[i];
        const valve_meta_t *m = &s_ctx.cfg.valves[id];
        if (!m->enabled || s_ctx.picked[id] == 0) {
            continue;
        }
        char part[32];
        snprintf(part, sizeof(part), " %s@%s", m->name, seat_label(m, s_ctx.picked[id]));
        if (strlen(buf) + strlen(part) < sizeof(buf) - 1) {
            strcat(buf, part);
        }
    }
    copy_bounded(s_ctx.solution, sizeof(s_ctx.solution), buf);
    char json[220];
    snprintf(json, sizeof(json), "{\"Solution\":\"%s\"}", s_ctx.solution);
    queue_puzzle_pub(TOPIC_VALVE_EVENTS, json);
}

static void darken_later_steps(const int *order, int from_index)
{
    bool any = false;
    for (int i = from_index; i < VALVE_COUNT; i++) {
        int id = order[i];
        if (!s_ctx.cfg.valves[id].enabled) {
            continue;
        }
        if (s_ctx.live[id].inlet != 0 || s_ctx.step_on[id]) {
            any = true;
        }
        s_ctx.step_on[id] = false;
        /* Keep picked[] and stored_inlet[] so a return can restore. */
        activate_valve(id, 0);
    }
    if (any) {
        queue_whoosh(false);
    }
}

static void mark_solved_locked(void)
{
    s_ctx.puzzle_solved = true;
    s_ctx.puzzle_running = false;
    copy_bounded(s_ctx.puzzle_state, sizeof(s_ctx.puzzle_state), "solved");
    queue_whoosh(true);
    queue_puzzle_pub(TOPIC_VALVE_EVENTS, "solved");
    build_solution_locked();
}

static void check_targets_locked(void)
{
    bool all = true;
    for (int i = 0; i < VALVE_COUNT; i++) {
        if (!s_ctx.cfg.valves[i].enabled) {
            continue;
        }
        /* Host owns power. Do not auto-enable. RND waits for first enable. */
        if (s_ctx.picked[i] == 0 || !valve_correct(i)) {
            s_ctx.step_on[i] = false;
            all = false;
        } else {
            s_ctx.step_on[i] = true;
        }
    }
    build_solution_locked();
    if (all) {
        mark_solved_locked();
    }
}

static void check_order_locked(void)
{
    int order[VALVE_COUNT];
    bool prefix = true;
    ordered_ids(order);

    for (int i = 0; i < VALVE_COUNT; i++) {
        int id = order[i];
        bool cfg_on = s_ctx.cfg.valves[id].enabled;
        bool ok = valve_correct(id);

        if (!cfg_on) {
            continue;
        }
        if (!prefix) {
            continue;
        }
        if (ok) {
            s_ctx.step_on[id] = true;
            if (is_two_seat(id)) {
                activate_valve(id, 3);
            }
            int ni = next_enabled_index(order, i);
            if (ni < 0) {
                mark_solved_locked();
                return;
            }
            int nid = order[ni];
            if (s_ctx.picked[nid] == 0) {
                assign_step(nid);
                queue_whoosh(true);
                build_solution_locked();
            } else if (!is_two_seat(nid) && s_ctx.live[nid].inlet == 0) {
                int inlet = s_ctx.stored_inlet[nid] > 0 ? s_ctx.stored_inlet[nid] : 1;
                activate_valve(nid, inlet);
            }
        } else {
            prefix = false;
            s_ctx.step_on[id] = false;
            if (is_two_seat(id)) {
                activate_valve(id, 0);
            }
            darken_later_steps(order, i + 1);
            build_solution_locked();
        }
    }
}

static void check_puzzle_locked(void)
{
    if (!s_ctx.puzzle_running || s_ctx.puzzle_solved) {
        return;
    }
    if (strcmp(s_ctx.cfg.game_mode, "target_positions") == 0) {
        check_targets_locked();
        return;
    }
    if (strcmp(s_ctx.cfg.game_mode, "valve_order") == 0) {
        check_order_locked();
    }
}

static void puzzle_clear_locked(void)
{
    s_ctx.puzzle_running = false;
    s_ctx.puzzle_solved = false;
    copy_bounded(s_ctx.puzzle_state, sizeof(s_ctx.puzzle_state), "ready");
    s_ctx.solution[0] = '\0';
    for (int i = 0; i < VALVE_COUNT; i++) {
        s_ctx.picked[i] = 0;
        s_ctx.stored_inlet[i] = 0;
        s_ctx.step_on[i] = false;
        s_ctx.gm_forced[i] = false;
    }
}

static void puzzle_reset_locked(void)
{
    puzzle_clear_locked();
    disable_prop();
    queue_puzzle_state("ready");
}

static void puzzle_start_locked(void)
{
    int order[VALVE_COUNT];
    int first;

    puzzle_clear_locked();
    enable_prop();
    ordered_ids(order);
    first = first_enabled_index(order);
    if (strcmp(s_ctx.cfg.game_mode, "target_positions") == 0) {
        /* Stay dark. Fixed targets are known now; RND waits for host enable. */
        for (int i = 0; i < VALVE_COUNT; i++) {
            const valve_meta_t *m = &s_ctx.cfg.valves[i];
            if (!m->enabled || strcmp(m->target, "rnd") == 0) {
                continue;
            }
            int t = atoi(m->target);
            if (t >= 1 && t <= seat_max(i)) {
                s_ctx.picked[i] = t;
            }
        }
    } else if (first >= 0) {
        int id = order[first];
        if (is_two_seat(id)) {
            s_ctx.picked[id] = pick_ab_target(s_ctx.live[id].position);
        } else {
            assign_step(id);
        }
    }
    s_ctx.puzzle_running = true;
    copy_bounded(s_ctx.puzzle_state, sizeof(s_ctx.puzzle_state), "running");
    queue_puzzle_state("running");
    build_solution_locked();
    check_puzzle_locked();
}

static void gm_force_valve_target_locked(int id)
{
    int pos = s_ctx.live[id].position;

    if (is_two_seat(id)) {
        if (pos != 1 && pos != 2) {
            pos = 1;
        }
        s_ctx.picked[id] = pos;
    } else {
        if (pos < 1 || pos > seat_max(id)) {
            pos = s_ctx.stored_inlet[id] > 0 ? s_ctx.stored_inlet[id] : 1;
        }
        s_ctx.picked[id] = pos;
        s_ctx.stored_inlet[id] = pos;
    }
    s_ctx.gm_forced[id] = true;
    s_ctx.step_on[id] = true;
    if (is_two_seat(id)) {
        activate_valve(id, 3);
    }
}

static void gm_solve_valve_locked(int id)
{
    if (id < 0 || id >= VALVE_COUNT || !s_ctx.cfg.valves[id].enabled) {
        return;
    }
    if (s_ctx.puzzle_solved) {
        return;
    }
    if (puzzle_mode_active() && !s_ctx.puzzle_running) {
        puzzle_start_locked();
    }
    gm_force_valve_target_locked(id);
    build_solution_locked();
    if (puzzle_mode_active() && s_ctx.puzzle_running) {
        check_puzzle_locked();
    } else if (!puzzle_mode_active()) {
        queue_whoosh(true);
    }
}

static void gm_solve_puzzle_locked(void)
{
    if (s_ctx.puzzle_solved) {
        queue_puzzle_pub(TOPIC_VALVE_EVENTS, "solved");
        return;
    }
    if (puzzle_mode_active() && !s_ctx.puzzle_running) {
        puzzle_start_locked();
    }
    for (int i = 0; i < VALVE_COUNT; i++) {
        if (!s_ctx.cfg.valves[i].enabled) {
            continue;
        }
        gm_force_valve_target_locked(i);
    }
    build_solution_locked();
    if (puzzle_mode_active()) {
        mark_solved_locked();
    } else {
        queue_whoosh(true);
        queue_puzzle_pub(TOPIC_VALVE_EVENTS, "solved");
        queue_puzzle_pub(TOPIC_VALVE_EVENTS, "{\"event\":\"solved\"}");
        s_ctx.puzzle_solved = true;
        copy_bounded(s_ctx.puzzle_state, sizeof(s_ctx.puzzle_state), "solved");
    }
}

static const char *valve_phase(int id)
{
    if (!puzzle_mode_active() || !s_ctx.cfg.valves[id].enabled) {
        return "on";
    }
    if (s_ctx.step_on[id] || (s_ctx.picked[id] && valve_correct(id))) {
        return "done";
    }
    if (s_ctx.picked[id] != 0) {
        return "active";
    }
    return "waiting";
}

static void scan_all(void)
{
    if (!s_ctx.prop_enabled || !mcp23s17_ready()) {
        return;
    }
    for (int i = 0; i < VALVE_COUNT; i++) {
        int pos = read_position(i, false);
        int need = s_ctx.cfg.debounce_count;
        if (need < 1) {
            need = 1;
        }
        /* Require consecutive identical samples. The old counter accepted any
         * mix of "not last published" and often reported OFF/next instead of
         * a brief target hit. Seated contacts confirm at `need`; OFF uses one
         * extra sample so a 10 ms seat is published before it is cleared. */
        if (pos != s_ctx.live[i].pending) {
            s_ctx.live[i].pending = pos;
            s_ctx.live[i].debounce = 1;
        } else if (s_ctx.live[i].debounce < 100) {
            s_ctx.live[i].debounce++;
        }
        if (pos == 0) {
            need += 1;
        }
        if (s_ctx.live[i].debounce >= need && pos != s_ctx.live[i].position) {
            publish_if_changed(i, pos, true);
        }
    }
}

static void scan_task(void *arg)
{
    bool mcp_started = false;
    (void)arg;
    while (true) {
        int period = 500;
        if (!mcp_started) {
            (void)mcp23s17_init();
            mcp_started = true;
            if (mcp23s17_ready() && valve_lock()) {
                for (int i = 0; i < VALVE_COUNT; i++) {
                    activate_valve(i, s_ctx.live[i].inlet);
                }
                valve_unlock();
                ESP_LOGI(TAG, "MCP23S17 cluster ready — panel I/O enabled");
            }
        } else if (!mcp23s17_ready()) {
            if (mcp23s17_probe() && valve_lock()) {
                for (int i = 0; i < VALVE_COUNT; i++) {
                    activate_valve(i, s_ctx.live[i].inlet);
                }
                valve_unlock();
                ESP_LOGI(TAG, "MCP23S17 cluster appeared — panel I/O enabled");
            }
        } else if (valve_lock()) {
            period = s_ctx.cfg.scan_period_ms;
            scan_all();
            valve_unlock();
        }
        {
            /* pdMS_TO_TICKS(5) is 0 at a 100 Hz tick; vTaskDelay(0) never
             * yields to IDLE and trips the task WDT. Always sleep ≥1 tick. */
            TickType_t ticks = pdMS_TO_TICKS(clamp_int(period, 5, 2000));
            vTaskDelay(ticks > 0 ? ticks : 1);
        }
    }
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s) — running with baked defaults", esp_err_to_name(err));
        return err;
    }
    s_ctx.spiffs_ready = true;
    return ESP_OK;
}

static void apply_valve_meta_json(cJSON *item, valve_meta_t *meta)
{
    cJSON *n;
    n = cJSON_GetObjectItemCaseSensitive(item, "enabled");
    if (cJSON_IsBool(n)) {
        meta->enabled = cJSON_IsTrue(n);
    }
    n = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(n) && n->valuestring) {
        copy_bounded(meta->name, sizeof(meta->name), n->valuestring);
    }
    n = cJSON_GetObjectItemCaseSensitive(item, "seats");
    if (cJSON_IsNumber(n)) {
        int seats = (int)n->valuedouble;
        if (seats == 2 || seats == 3 || seats == 4) {
            meta->seats = seats;
        }
    }
    n = cJSON_GetObjectItemCaseSensitive(item, "color");
    if (cJSON_IsString(n) && n->valuestring) {
        copy_bounded(meta->color, sizeof(meta->color), n->valuestring);
    }
    n = cJSON_GetObjectItemCaseSensitive(item, "order");
    if (cJSON_IsNumber(n)) {
        meta->order = clamp_int((int)n->valuedouble, 1, 8);
    }
    n = cJSON_GetObjectItemCaseSensitive(item, "target");
    if (cJSON_IsString(n) && n->valuestring) {
        if (strcasecmp(n->valuestring, "rnd") == 0 || strcmp(n->valuestring, "-1") == 0) {
            copy_bounded(meta->target, sizeof(meta->target), "rnd");
        } else {
            int t = atoi(n->valuestring);
            if (t >= 1 && t <= 4) {
                snprintf(meta->target, sizeof(meta->target), "%d", t);
            } else {
                copy_bounded(meta->target, sizeof(meta->target), "rnd");
            }
        }
    } else if (cJSON_IsNumber(n)) {
        int t = (int)n->valuedouble;
        if (t >= 1 && t <= 4) {
            snprintf(meta->target, sizeof(meta->target), "%d", t);
        } else {
            copy_bounded(meta->target, sizeof(meta->target), "rnd");
        }
    }
}

static void apply_valves_array(cJSON *arr, valve_meta_t *dest)
{
    cJSON *item = NULL;
    int sequential = 0;
    if (!cJSON_IsArray(arr) || !dest) {
        return;
    }
    cJSON_ArrayForEach(item, arr) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        int idx = sequential;
        if (cJSON_IsNumber(id)) {
            idx = (int)id->valuedouble;
        }
        if (idx >= 0 && idx < VALVE_COUNT) {
            apply_valve_meta_json(item, &dest[idx]);
        }
        sequential++;
    }
}

static void apply_slot_csv(const char *csv, valve_meta_t *dest)
{
    char buf[512];
    char *save = NULL;
    char *tok;
    int idx = 0;
    if (!csv || !dest) {
        return;
    }
    copy_bounded(buf, sizeof(buf), csv);
    tok = strtok_r(buf, ";", &save);
    while (tok && idx < VALVE_COUNT) {
        char name[8] = {0};
        char color[12] = {0};
        char target[8] = {0};
        int enabled = 1;
        int seats = 4;
        int order = idx + 1;
        if (sscanf(tok, "%d,%7[^,],%d,%11[^,],%d,%7s",
                   &enabled, name, &seats, color, &order, target) >= 6) {
            dest[idx].enabled = enabled != 0;
            copy_bounded(dest[idx].name, sizeof(dest[idx].name), name);
            dest[idx].seats = (seats == 2 || seats == 3 || seats == 4) ? seats : 4;
            copy_bounded(dest[idx].color, sizeof(dest[idx].color), color);
            dest[idx].order = clamp_int(order, 1, 8);
            if (strcasecmp(target, "rnd") == 0) {
                copy_bounded(dest[idx].target, sizeof(dest[idx].target), "rnd");
            } else {
                int t = atoi(target);
                if (t >= 1 && t <= 4) {
                    snprintf(dest[idx].target, sizeof(dest[idx].target), "%d", t);
                } else {
                    copy_bounded(dest[idx].target, sizeof(dest[idx].target), "rnd");
                }
            }
        }
        idx++;
        tok = strtok_r(NULL, ";", &save);
    }
}

static void apply_slot_json(cJSON *node, valve_meta_t *dest)
{
    if (cJSON_IsString(node) && node->valuestring) {
        apply_slot_csv(node->valuestring, dest);
    } else if (cJSON_IsArray(node)) {
        apply_valves_array(node, dest);
    }
}

static void format_slot_csv(char *out, size_t out_size, const valve_meta_t *valves)
{
    size_t pos = 0;
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    for (int i = 0; i < VALVE_COUNT; i++) {
        int n = snprintf(out + pos, out_size - pos, "%s%d,%s,%d,%s,%d,%s",
                         i ? ";" : "",
                         valves[i].enabled ? 1 : 0,
                         valves[i].name,
                         valves[i].seats,
                         valves[i].color,
                         valves[i].order,
                         valves[i].target);
        if (n < 0 || (size_t)n >= out_size - pos) {
            break;
        }
        pos += (size_t)n;
    }
}

static void apply_config_fields(cJSON *root, bool apply_valves_without_presets)
{
    cJSON *n;
    cJSON *presets;
    bool has_presets;
    char slot_buf[2];
    if (!root) {
        return;
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "gameMode");
    if (cJSON_IsString(n) && n->valuestring) {
        char prev_mode[24];
        copy_bounded(prev_mode, sizeof(prev_mode), s_ctx.cfg.game_mode);
        if (strcmp(n->valuestring, "valve_order") == 0 ||
            strcmp(n->valuestring, "target_positions") == 0 ||
            strcmp(n->valuestring, "io") == 0) {
            copy_bounded(s_ctx.cfg.game_mode, sizeof(s_ctx.cfg.game_mode), n->valuestring);
        } else if (strcmp(n->valuestring, "fixed_sequence") == 0) {
            copy_bounded(s_ctx.cfg.game_mode, sizeof(s_ctx.cfg.game_mode), "valve_order");
        }
        if (strcmp(prev_mode, s_ctx.cfg.game_mode) != 0) {
            puzzle_reset_locked();
        }
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "scanPeriodMs");
    if (cJSON_IsNumber(n)) {
        s_ctx.cfg.scan_period_ms = clamp_int((int)n->valuedouble, 5, 200);
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "debounceCount");
    if (cJSON_IsNumber(n)) {
        s_ctx.cfg.debounce_count = clamp_int((int)n->valuedouble, 1, 20);
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "settleMs");
    if (cJSON_IsNumber(n)) {
        s_ctx.cfg.settle_ms = clamp_int((int)n->valuedouble, 10, 200);
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "heartbeatInterval");
    if (cJSON_IsNumber(n)) {
        s_ctx.cfg.heartbeat_interval_ms = clamp_int((int)n->valuedouble, 1000, 120000);
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "debug");
    if (cJSON_IsBool(n)) {
        s_ctx.cfg.debug = cJSON_IsTrue(n);
    }

    n = cJSON_GetObjectItemCaseSensitive(root, "gameSettings");
    if (cJSON_IsObject(n)) {
        for (int m = 0; m < MODE_COUNT; m++) {
            cJSON *sel = cJSON_GetObjectItemCaseSensitive(n, s_mode_keys[m]);
            if (cJSON_IsString(sel) && sel->valuestring) {
                s_ctx.cfg.selected[m] = (char)('A' + slot_index_from_str(sel->valuestring));
            }
        }
    }

    n = cJSON_GetObjectItemCaseSensitive(root, "gameSetting");
    if (cJSON_IsString(n) && n->valuestring) {
        int m = mode_index(s_ctx.cfg.game_mode);
        s_ctx.cfg.selected[m] = (char)('A' + slot_index_from_str(n->valuestring));
    }

    presets = cJSON_GetObjectItemCaseSensitive(root, "modePresets");
    has_presets = cJSON_IsObject(presets);
    if (has_presets) {
        for (int m = 0; m < MODE_COUNT; m++) {
            cJSON *mode_obj = cJSON_GetObjectItemCaseSensitive(presets, s_mode_keys[m]);
            if (!cJSON_IsObject(mode_obj)) {
                continue;
            }
            for (int s = 0; s < SLOT_COUNT; s++) {
                slot_buf[0] = (char)('A' + s);
                slot_buf[1] = '\0';
                apply_slot_json(cJSON_GetObjectItemCaseSensitive(mode_obj, slot_buf),
                                s_ctx.cfg.slots[m][s]);
            }
        }
        sync_active_from_slot(&s_ctx.cfg);
    }

    /* Legacy files without modePresets keep valve4 slot A; do not stomp A
     * with the old single valves table. When presets are present, valves is
     * the currently selected slot's editor copy. */
    n = cJSON_GetObjectItemCaseSensitive(root, "valves");
    if (cJSON_IsArray(n) && (has_presets || apply_valves_without_presets)) {
        apply_valves_array(n, s_ctx.cfg.valves);
        sync_slot_from_active(&s_ctx.cfg);
    }
}

static void load_config_file(void)
{
    FILE *f;
    long size;
    char *buf;
    cJSON *root;

    if (!s_ctx.spiffs_ready) {
        return;
    }
    f = fopen(CONFIG_PATH, "r");
    if (!f) {
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    size = ftell(f);
    rewind(f);
    if (size <= 0 || size > CONFIG_FILE_MAX) {
        fclose(f);
        return;
    }
    buf = (char *)calloc(1, (size_t)size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return;
    }
    fclose(f);
    root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return;
    }
    apply_config_fields(root, false);
    cJSON_Delete(root);
}

static cJSON *valve_meta_to_json(const valve_meta_t *m, int id)
{
    cJSON *v = cJSON_CreateObject();
    if (!v || !m) {
        return v;
    }
    cJSON_AddNumberToObject(v, "id", id);
    cJSON_AddBoolToObject(v, "enabled", m->enabled);
    cJSON_AddStringToObject(v, "name", m->name);
    cJSON_AddNumberToObject(v, "seats", m->seats);
    cJSON_AddStringToObject(v, "color", m->color);
    cJSON_AddNumberToObject(v, "order", m->order);
    if (strcmp(m->target, "rnd") == 0) {
        cJSON_AddStringToObject(v, "target", "rnd");
    } else {
        cJSON_AddNumberToObject(v, "target", atoi(m->target));
    }
    return v;
}

static void append_valves_from(cJSON *root, const char *key, const valve_meta_t *valves, bool include_live)
{
    cJSON *arr = cJSON_AddArrayToObject(root, key);
    for (int i = 0; i < VALVE_COUNT; i++) {
        const valve_meta_t *m = &valves[i];
        cJSON *v = valve_meta_to_json(m, i);
        if (include_live) {
            int pos = s_ctx.live[i].position;
            int inlet = s_ctx.live[i].inlet;
            cJSON *reeds = cJSON_AddArrayToObject(v, "reeds");
            cJSON_AddNumberToObject(v, "position", pos);
            cJSON_AddNumberToObject(v, "inlet", inlet);
            cJSON_AddBoolToObject(v, "power", inlet > 0);
            cJSON_AddStringToObject(v, "posLabel", seat_label(m, pos));
            cJSON_AddStringToObject(v, "phase", valve_phase(i));
            cJSON_AddNumberToObject(v, "pickedTarget", s_ctx.picked[i]);
            if (s_ctx.picked[i] > 0) {
                cJSON_AddNumberToObject(v, "resolvedTarget", s_ctx.picked[i]);
                cJSON_AddStringToObject(v, "targetLabel", seat_label(m, s_ctx.picked[i]));
            } else if (strcmp(m->target, "rnd") == 0) {
                cJSON_AddStringToObject(v, "targetLabel", "RND");
            } else {
                cJSON_AddStringToObject(v, "targetLabel", seat_label(m, atoi(m->target)));
            }
            for (int r = 0; r < 4; r++) {
                int bit = mcp23s17_read(s_hw[i].reed_start + r);
                if (bit < 0) {
                    bit = 1;
                }
                cJSON_AddItemToArray(reeds, cJSON_CreateNumber(bit));
            }
        }
        cJSON_AddItemToArray(arr, v);
    }
}

static void append_valves_json(cJSON *root, bool include_live)
{
    append_valves_from(root, "valves", s_ctx.cfg.valves, include_live);
}

static void append_mode_presets_json(cJSON *root, const valve_config_t *cfg)
{
    cJSON *presets = cJSON_AddObjectToObject(root, "modePresets");
    cJSON *sel = cJSON_AddObjectToObject(root, "gameSettings");
    char letter[2] = {0};
    int current = mode_index(cfg->game_mode);
    letter[0] = cfg->selected[current];
    cJSON_AddStringToObject(root, "gameSetting", letter);
    for (int m = 0; m < MODE_COUNT; m++) {
        cJSON *mode_obj = cJSON_AddObjectToObject(presets, s_mode_keys[m]);
        letter[0] = cfg->selected[m];
        cJSON_AddStringToObject(sel, s_mode_keys[m], letter);
        for (int s = 0; s < SLOT_COUNT; s++) {
            char csv[512];
            letter[0] = (char)('A' + s);
            format_slot_csv(csv, sizeof(csv), cfg->slots[m][s]);
            cJSON_AddStringToObject(mode_obj, letter, csv);
        }
    }
}

static void append_pins_json(cJSON *root)
{
    static char role_buf[48][12];
    static bool inited;
    if (!inited) {
        for (int p = 0; p < 48; p++) {
            copy_bounded(role_buf[p], sizeof(role_buf[p]), "—");
        }
        for (int i = 0; i < VALVE_COUNT; i++) {
            snprintf(role_buf[s_hw[i].power], sizeof(role_buf[0]), "pwr%d", i);
            for (int r = 0; r < 4; r++) {
                snprintf(role_buf[s_hw[i].reed_start + r], sizeof(role_buf[0]), "v%d r%d", i, r + 1);
            }
        }
        inited = true;
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "pins");
    for (int p = 0; p < 48; p++) {
        cJSON *pin = cJSON_CreateObject();
        int mode = mcp23s17_get_mode(p);
        int level = (mode == 0) ? mcp23s17_get_olat(p) : mcp23s17_read(p);
        if (level < 0) {
            level = 1;
        }
        cJSON_AddNumberToObject(pin, "logical", p);
        cJSON_AddNumberToObject(pin, "chip", p / 16);
        cJSON_AddStringToObject(pin, "role", role_buf[p]);
        cJSON_AddStringToObject(pin, "mode", mode == 0 ? "out" : "in");
        cJSON_AddNumberToObject(pin, "level", level);
        cJSON_AddItemToArray(arr, pin);
    }
}

static char *print_root(cJSON *root)
{
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

static void copy_json_out(char *out, size_t out_size, cJSON *root)
{
    char *txt = print_root(root);
    if (!out || out_size == 0) {
        free(txt);
        return;
    }
    if (!txt) {
        copy_bounded(out, out_size, "{}");
        return;
    }
    copy_bounded(out, out_size, txt);
    cJSON_free(txt);
}

static esp_err_t persist_engine_fields(void)
{
    FILE *f;
    long size = 0;
    char *existing = NULL;
    cJSON *root;
    char *merged;

    if (!s_ctx.spiffs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    f = fopen(CONFIG_PATH, "r");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            size = ftell(f);
            rewind(f);
        }
        if (size > 0 && size <= CONFIG_FILE_MAX) {
            existing = (char *)calloc(1, (size_t)size + 1);
            if (existing && fread(existing, 1, (size_t)size, f) == (size_t)size) {
                root = cJSON_Parse(existing);
            } else {
                root = NULL;
            }
            free(existing);
        } else {
            root = NULL;
        }
        fclose(f);
    } else {
        root = NULL;
    }
    if (!root) {
        root = cJSON_CreateObject();
    }
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_DeleteItemFromObjectCaseSensitive(root, "gameMode");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "gameSetting");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "gameSettings");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "modePresets");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "scanPeriodMs");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "debounceCount");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "settleMs");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "heartbeatInterval");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "debug");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "valves");
    cJSON_AddStringToObject(root, "gameMode", s_ctx.cfg.game_mode);
    cJSON_AddNumberToObject(root, "scanPeriodMs", s_ctx.cfg.scan_period_ms);
    cJSON_AddNumberToObject(root, "debounceCount", s_ctx.cfg.debounce_count);
    cJSON_AddNumberToObject(root, "settleMs", s_ctx.cfg.settle_ms);
    cJSON_AddNumberToObject(root, "heartbeatInterval", s_ctx.cfg.heartbeat_interval_ms);
    cJSON_AddBoolToObject(root, "debug", s_ctx.cfg.debug);
    append_mode_presets_json(root, &s_ctx.cfg);
    append_valves_json(root, false);

    merged = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!merged) {
        return ESP_ERR_NO_MEM;
    }
    f = fopen(CONFIG_PATH, "w");
    if (!f) {
        cJSON_free(merged);
        return ESP_FAIL;
    }
    size_t n = strlen(merged);
    if (fwrite(merged, 1, n, f) != n) {
        fclose(f);
        cJSON_free(merged);
        return ESP_FAIL;
    }
    fclose(f);
    cJSON_free(merged);
    return ESP_OK;
}

esp_err_t valve_engine_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    set_default_config(&s_ctx.cfg);
    s_ctx.lock = xSemaphoreCreateMutex();
    if (!s_ctx.lock) {
        return ESP_ERR_NO_MEM;
    }

    (void)init_spiffs();
    load_config_file();

    for (int i = 0; i < VALVE_COUNT; i++) {
        s_ctx.live[i].position = 0;
        s_ctx.live[i].inlet = 0;
    }

    copy_bounded(s_ctx.puzzle_state, sizeof(s_ctx.puzzle_state), "ready");

    ESP_LOGI(TAG, "Valve engine initialized (scan starts after Wi-Fi)");
    return ESP_OK;
}

esp_err_t valve_engine_start(void)
{
    if (xTaskCreate(scan_task, "valve_scan", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void valve_engine_get_state_json(char *out, size_t out_size)
{
    cJSON *root;
    const esp_app_desc_t *app = esp_app_get_description();
    if (!valve_lock()) {
        copy_bounded(out, out_size, "{}");
        return;
    }
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", (double)now_ms());
    cJSON_AddStringToObject(root, "id", "Valve32Prop");
    cJSON_AddStringToObject(root, "status", s_ctx.prop_enabled ? "online" : "paused");
    cJSON_AddStringToObject(root, "version", app ? app->version : "0.01");
    cJSON_AddBoolToObject(root, "enabled", s_ctx.prop_enabled);
    {
        bool hw_ok = mcp23s17_ready();
        char hw_fault[128];
        mcp23s17_get_fault(hw_fault, sizeof(hw_fault));
        cJSON_AddBoolToObject(root, "hwOk", hw_ok);
        if (!hw_ok && hw_fault[0]) {
            cJSON_AddStringToObject(root, "hwFault", hw_fault);
        }
    }
    cJSON_AddStringToObject(root, "gameMode", s_ctx.cfg.game_mode);
    cJSON_AddStringToObject(root, "puzzleState",
                            s_ctx.puzzle_state[0] ? s_ctx.puzzle_state
                                                 : (s_ctx.prop_enabled ? "running" : "paused"));
    cJSON_AddBoolToObject(root, "solved", s_ctx.puzzle_solved);
    cJSON_AddStringToObject(root, "solution", s_ctx.solution);
    cJSON_AddBoolToObject(root, "wifiConnected", s_ctx.wifi_connected);
    cJSON_AddStringToObject(root, "wifiSsid", s_ctx.wifi_ssid);
    cJSON_AddNumberToObject(root, "wifiRssi", s_ctx.wifi_rssi);
    append_valves_json(root, true);
    append_pins_json(root);
    valve_unlock();
    copy_json_out(out, out_size, root);
}

void valve_engine_get_config_json(char *out, size_t out_size)
{
    cJSON *root;
    if (!valve_lock()) {
        copy_bounded(out, out_size, "{}");
        return;
    }
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gameMode", s_ctx.cfg.game_mode);
    cJSON_AddNumberToObject(root, "scanPeriodMs", s_ctx.cfg.scan_period_ms);
    cJSON_AddNumberToObject(root, "debounceCount", s_ctx.cfg.debounce_count);
    cJSON_AddNumberToObject(root, "settleMs", s_ctx.cfg.settle_ms);
    cJSON_AddNumberToObject(root, "heartbeatInterval", s_ctx.cfg.heartbeat_interval_ms);
    cJSON_AddBoolToObject(root, "debug", s_ctx.cfg.debug);
    append_mode_presets_json(root, &s_ctx.cfg);
    append_valves_json(root, false);
    valve_unlock();
    copy_json_out(out, out_size, root);
}

int valve_engine_get_heartbeat_interval_ms(void)
{
    int ms = 10000;
    if (valve_lock()) {
        ms = s_ctx.cfg.heartbeat_interval_ms;
        valve_unlock();
    }
    return clamp_int(ms, 1000, 120000);
}

void valve_engine_get_default_config_json(char *out, size_t out_size)
{
    valve_config_t *tmp;
    cJSON *root;
    tmp = (valve_config_t *)calloc(1, sizeof(*tmp));
    if (!tmp) {
        copy_bounded(out, out_size, "{}");
        return;
    }
    set_default_config(tmp);
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "gameMode", tmp->game_mode);
    cJSON_AddNumberToObject(root, "scanPeriodMs", tmp->scan_period_ms);
    cJSON_AddNumberToObject(root, "debounceCount", tmp->debounce_count);
    cJSON_AddNumberToObject(root, "settleMs", tmp->settle_ms);
    cJSON_AddNumberToObject(root, "heartbeatInterval", tmp->heartbeat_interval_ms);
    cJSON_AddBoolToObject(root, "debug", tmp->debug);
    append_mode_presets_json(root, tmp);
    append_valves_from(root, "valves", tmp->valves, false);
    free(tmp);
    copy_json_out(out, out_size, root);
}

esp_err_t valve_engine_handle_command_json(const char *json, char *response, size_t response_size)
{
    cJSON *root;
    cJSON *n;
    const char *cmd = NULL;
    int valve_id = -1;
    int inlet = -1;
    int read_id = -1;

    if (!json) {
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"empty\"}");
        return ESP_ERR_INVALID_ARG;
    }
    root = cJSON_Parse(json);
    if (!root) {
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"json\"}");
        return ESP_ERR_INVALID_ARG;
    }

    n = cJSON_GetObjectItemCaseSensitive(root, "Command");
    if (!cJSON_IsString(n)) {
        n = cJSON_GetObjectItemCaseSensitive(root, "command");
    }
    if (cJSON_IsString(n)) {
        cmd = n->valuestring;
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "Valve");
    if (!cJSON_IsNumber(n)) {
        n = cJSON_GetObjectItemCaseSensitive(root, "valve");
    }
    if (cJSON_IsNumber(n)) {
        valve_id = (int)n->valuedouble;
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "Inlet");
    if (cJSON_IsNumber(n)) {
        inlet = (int)n->valuedouble;
    }
    n = cJSON_GetObjectItemCaseSensitive(root, "ReadValve");
    if (cJSON_IsNumber(n)) {
        read_id = (int)n->valuedouble;
    }

    if (!valve_lock()) {
        cJSON_Delete(root);
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }

    if (cmd && puzzle_mode_active() &&
        (strcmp(cmd, "start") == 0 || strcmp(cmd, "reset") == 0 ||
         strcmp(cmd, "stop") == 0 || strcmp(cmd, "getSolution") == 0)) {
        if (strcmp(cmd, "start") == 0) {
            puzzle_start_locked();
        } else if (strcmp(cmd, "getSolution") == 0) {
            build_solution_locked();
        } else {
            puzzle_reset_locked();
        }
        copy_bounded(response, response_size, "{\"ok\":true}");
    } else if (cmd && strcmp(cmd, "reset") == 0) {
        puzzle_reset_locked();
        copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"reset\"}");
    } else if (cmd && (strcmp(cmd, "solve") == 0 || strcmp(cmd, "solvePuzzle") == 0)) {
        gm_solve_puzzle_locked();
        copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"solve\"}");
    } else if (cmd && strcmp(cmd, "solveValve") == 0) {
        if (valve_id < 0 || valve_id >= VALVE_COUNT) {
            copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"valve\"}");
        } else {
            gm_solve_valve_locked(valve_id);
            copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"solveValve\"}");
        }
    } else if (cmd && strcmp(cmd, "enable") == 0) {
        enable_prop();
        copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"enable\"}");
    } else if (cmd && strcmp(cmd, "disable") == 0) {
        disable_prop();
        copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"disable\"}");
    } else if (cmd && strcmp(cmd, "forceScan") == 0) {
        force_scan_all();
        copy_bounded(response, response_size, "{\"ok\":true,\"Command\":\"forceScan\"}");
    } else if (valve_id >= 0 && valve_id < VALVE_COUNT && inlet >= 0 && inlet <= 4) {
        activate_valve(valve_id, inlet);
        if (s_ctx.puzzle_running &&
            strcmp(s_ctx.cfg.game_mode, "target_positions") == 0 &&
            inlet > 0) {
            pick_target_positions_on_enable(valve_id);
        }
        if (s_ctx.puzzle_running) {
            check_puzzle_locked();
        }
        copy_bounded(response, response_size, "{\"ok\":true}");
    } else if (read_id >= 0 && read_id < VALVE_COUNT) {
        int pos = read_position(read_id, true);
        publish_if_changed(read_id, pos, true);
        copy_bounded(response, response_size, "{\"ok\":true}");
    } else if (read_id != -1) {
        ESP_LOGW(TAG, "ReadValve %d out of bounds", read_id);
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"bounds\"}");
    } else {
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"unknown\"}");
    }

    valve_unlock();
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t valve_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    if (!root) {
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"json\"}");
        return ESP_ERR_INVALID_ARG;
    }
    if (!valve_lock()) {
        cJSON_Delete(root);
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    apply_config_fields(root, true);
    if (persist) {
        (void)persist_engine_fields();
    }
    valve_unlock();
    cJSON_Delete(root);
    copy_bounded(response, response_size, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t valve_engine_restore_defaults(bool persist, char *response, size_t response_size)
{
    if (!valve_lock()) {
        copy_bounded(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    set_default_config(&s_ctx.cfg);
    if (persist) {
        (void)persist_engine_fields();
    }
    valve_unlock();
    copy_bounded(response, response_size, "{\"ok\":true}");
    return ESP_OK;
}

bool valve_engine_owns_puzzle(void)
{
    bool owns = false;
    if (valve_lock()) {
        owns = puzzle_mode_active();
        valve_unlock();
    }
    return owns;
}

esp_err_t valve_engine_handle_puzzle_command_json(const char *json, char *response, size_t response_size)
{
    return valve_engine_handle_command_json(json, response, response_size);
}

bool valve_engine_pop_puzzle_pub(char *topic, size_t topic_size, char *payload, size_t payload_size)
{
    if (!valve_lock()) {
        return false;
    }
    if (s_ctx.pub_tail == s_ctx.pub_head) {
        valve_unlock();
        return false;
    }
    copy_bounded(topic, topic_size, s_ctx.pubs[s_ctx.pub_tail].topic);
    copy_bounded(payload, payload_size, s_ctx.pubs[s_ctx.pub_tail].payload);
    s_ctx.pub_tail = (s_ctx.pub_tail + 1) % PUZZLE_PUB_LEN;
    valve_unlock();
    return true;
}

bool valve_engine_pop_event_json(char *out, size_t out_size)
{
    if (!valve_lock()) {
        return false;
    }
    if (s_ctx.event_tail == s_ctx.event_head) {
        valve_unlock();
        return false;
    }
    copy_bounded(out, out_size, s_ctx.event_queue[s_ctx.event_tail]);
    s_ctx.event_tail = (s_ctx.event_tail + 1) % EVENT_QUEUE_LEN;
    valve_unlock();
    return true;
}

bool valve_engine_pop_warning_message(char *out, size_t out_size)
{
    (void)out;
    (void)out_size;
    return false;
}

void valve_engine_notify_wifi_connected(int rssi)
{
    if (!valve_lock()) {
        return;
    }
    s_ctx.wifi_connected = true;
    s_ctx.wifi_rssi = rssi;
    valve_unlock();
}

void valve_engine_notify_wifi_disconnected(void)
{
    if (!valve_lock()) {
        return;
    }
    s_ctx.wifi_connected = false;
    valve_unlock();
}

void valve_engine_set_wifi_ssid(const char *ssid)
{
    if (!valve_lock()) {
        return;
    }
    copy_bounded(s_ctx.wifi_ssid, sizeof(s_ctx.wifi_ssid), ssid);
    valve_unlock();
}

valve_led_hint_t valve_engine_get_led_hint(void)
{
    return VALVE_LED_HINT_OFF;
}

void valve_engine_get_battery_snapshot(valve_battery_snapshot_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->wire_count = 0;
    out->battery_adc_raw = 0;
    copy_bounded(out->battery_profile, sizeof(out->battery_profile), "none");
}
