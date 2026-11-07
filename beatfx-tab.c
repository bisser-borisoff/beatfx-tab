/* SPDX-License-Identifier: GPL-3.0-only
 *
 * beatfx-tab: the collapsed BEAT FX tab on the 2 DECK waveform screen shows the selected
 * effect, its beat value and where it goes (CH SELECT). README.md says what it shows and how
 * it is built.
 *
 * Threads:
 *   - the mixer bus thread runs mixer_frame(): it packs the Beat FX report into g_fx;
 *   - the touch thread runs on_touch(): it follows the panel's state in g_panel;
 *   - the core's overlay thread runs tick() (visibility, paced repaints) and the surface's
 *     paint, one after the other, never at the same time.
 * They share only the three atomics below. paint also reads s_now, a plain static that tick
 * writes (tick alone keeps s_confirmed): both run on the overlay thread (AZ-MODS 1.2.8:
 * community_tick, then sdk_overlay_present, in one loop). The mod starts no thread of its own. The Mod Menu
 * row (v1.1) is read with row_value, one atomic load that is safe from any thread, in tick
 * and in paint.
 *
 * It only reads: frames go on unchanged, and every touch goes on to the deck.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "azmod-mod.h"
#include "fx_decode.h"
#include "geometry.h"
#include "label_draw.h"
#include "panel_track.h"

#define MOD_ID "beatfx-tab"
#define MIN_REDRAW_MS 100 /* at most 10 repaints a second */

static const az_core_v1 *C;
static az_surface *g_label;

static _Atomic uint64_t g_fx;                   /* fxd_pack of the last usable report */
static _Atomic int g_panel = PANEL_BOOT_STATE;  /* PT_* */
static _Atomic int g_in_wave2;                  /* 1 = 2 DECK waveform screen (set by tick) */

/* v1.1, Mod Menu: the channel row on or off (on if the row could not be registered). The
 * label always sits over the stock tab. */
static int g_row_channel = -1;

static label_opts current_opts(void)
{
    return (label_opts){ g_row_channel < 0 || C->row_value(g_row_channel) == 1 };
}

/* ---- bus threads: microseconds, no blocking ---- */

static int mixer_frame(int bus, unsigned char *frame, int len, void *user)
{
    (void)user;
    if (bus == AZ_BUS_MIXER_IN) {
        uint64_t p = fxd_pack(frame, len);
        if (p) atomic_store_explicit(&g_fx, p, memory_order_relaxed);
    }
    return AZ_FRAME_OK;
}

static int on_touch(const az_touch *t, void *user)
{
    (void)user;
    if (t && t->down && atomic_load_explicit(&g_in_wave2, memory_order_relaxed)) {
        int s = atomic_load_explicit(&g_panel, memory_order_relaxed);
        atomic_store_explicit(&g_panel, pt_on_touch(s, t->x, t->y), memory_order_relaxed);
    }
    return AZ_TOUCH_PASS;
}

/* ---- the overlay thread ---- */

/* tick decides what the label shows and paint draws exactly that. s_confirmed is the last
 * report whose beat was confirmed: after a CH SELECT press the mixer sends the new channel's
 * BPM with the old TIME, and the label keeps that beat (fxd_decode_held). */
static uint64_t s_confirmed;
static fxd_state s_now; /* valid = 0 until the first tick */

static void paint(az_draw *d, az_rect area, void *user)
{
    (void)user;
    label_paint(C, d, area, s_now, current_opts());
}

static const az_surface_v1 LABEL = {
    .size = sizeof(az_surface_v1),
    .layer = AZ_LAYER_OVER_EP147,
    .views = AZ_VIEW_BIT(AZ_VIEW_DECK),
    .on_paint = paint,
    .on_input = NULL, /* taps go on to the deck's own tab underneath */
    .user = NULL,
};

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* What the label shows; a change in BPM alone keeps it the same, so does TIME while a value
 * shows (ms is 0 then), and so does the channel while the channel row is off. */
static int same_look(fxd_state a, fxd_state b, label_opts o)
{
    return a.valid == b.valid && a.named == b.named && a.effect == b.effect && a.on == b.on &&
           a.percent == b.percent && a.beat == b.beat && a.ms == b.ms && a.xpad == b.xpad &&
           (!o.row || a.ch == b.ch);
}

static int same_opts(label_opts a, label_opts b)
{
    return a.row == b.row;
}

/* Why the label is hidden, logged when it changes so "it vanished" can be read back from
 * /tmp/azmod-community/beatfx-tab.log (b.sr, 2026-11-06; the core writes it only with its
 * diagnostics flag on). At most one line every LOG_MIN_MS: a burst of changes (a knob turn,
 * taps) writes the state it ends in, not every step. LOG_MAX lines a run at most. */
enum { WHY_SHOWN, WHY_VIEW, WHY_REPORT, WHY_PANEL_OPEN, WHY_PANEL_UNKNOWN };
#define LOG_MAX 2000
#define LOG_MIN_MS 250
static int s_logged_why = -1, s_logged;
static long s_log_ms = -LOG_MIN_MS;

static void log_why(int why, uint64_t p, long now)
{
    if (why == s_logged_why || s_logged >= LOG_MAX || now - s_log_ms < LOG_MIN_MS) return;
    s_logged_why = why;
    s_log_ms = now;
    s_logged++;
    switch (why) {
    case WHY_SHOWN: C->log(MOD_ID, "shown"); break;
    case WHY_VIEW:
        C->log(MOD_ID, "hidden: not the 2 DECK waveform view (view %d, %d decks)", C->view(),
               C->deck_count());
        break;
    case WHY_REPORT:
        C->log(MOD_ID, "hidden: no usable Beat FX report (0x%016llx)", (unsigned long long)p);
        break;
    case WHY_PANEL_OPEN: C->log(MOD_ID, "hidden: panel open"); break;
    default: C->log(MOD_ID, "hidden: panel state unknown (until a tap on the tab or the handle)");
    }
}

static int s_visible;
static int s_pending;
static long s_last_redraw = -MIN_REDRAW_MS;
static fxd_state s_shown_look;
static label_opts s_shown_opts;

static void tick(void)
{
    int wave2 = C->view() == AZ_VIEW_DECK && C->deck_count() == 2;
    atomic_store_explicit(&g_in_wave2, wave2, memory_order_relaxed);

    label_opts o = current_opts();
    uint64_t p = atomic_load_explicit(&g_fx, memory_order_relaxed);
    fxd_state s = s_now = fxd_decode_held(&s_confirmed, p);
    int panel = atomic_load_explicit(&g_panel, memory_order_relaxed);
    int why = !wave2 ? WHY_VIEW
            : !(s.valid || s.named) ? WHY_REPORT
            : panel == PT_OPEN ? WHY_PANEL_OPEN
            : panel != PT_CLOSED ? WHY_PANEL_UNKNOWN
            : WHY_SHOWN;
    long t = now_ms();
    log_why(why, p, t);
    int visible = why == WHY_SHOWN;
    if (visible != s_visible) {
        s_visible = visible;
        C->surface_show(g_label, visible);
        if (visible) s_pending = 1; /* a fresh show always paints the current state */
    }
    if (!visible) return;
    if (!same_look(s, s_shown_look, o) || !same_opts(o, s_shown_opts)) s_pending = 1;

    if (s_pending && t - s_last_redraw >= MIN_REDRAW_MS) {
        s_pending = 0;
        s_last_redraw = t;
        s_shown_look = s;
        s_shown_opts = o;
        C->surface_redraw(g_label);
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, view) || !AZ_HAS(C, deck_count)) {
        C->log(MOD_ID, "this core lacks a service beatfx-tab needs (AZ-MODS 1.2.3 or newer)");
        return 2;
    }
    g_label = C->surface_create(MOD_ID, &LABEL);
    if (!g_label) return 3; /* the core logged why */
    C->surface_place(g_label, (az_rect){ LABEL_X, LABEL_Y, LABEL_W, LABEL_H });
    C->surface_show(g_label, 0);
    /* Without either filter the mod stays loaded and inert: no report or no panel state
     * means the label never shows (AZ-MODS 1.2.3+ has both; the SDK's harness has neither). */
    if (!AZ_HAS(C, frame_filter) ||
        C->frame_filter(MOD_ID, (1u << AZ_BUS_MIXER_IN) | AZ_FILTER_RAW, mixer_frame, NULL) != 0)
        C->log(MOD_ID, "no mixer frames: the label stays hidden");
    if (!AZ_HAS(C, touch_filter) || C->touch_filter(MOD_ID, on_touch, NULL) != 0)
        C->log(MOD_ID, "no touches: the label stays hidden");
    if (AZ_HAS(C, register_row) && AZ_HAS(C, row_value)) {
        const az_row_v1 row = {
            .size = sizeof(az_row_v1),
            .key = "channel-row",
            .label = "Beat FX channel row",
            .kind = AZ_ROW_TOGGLE,
            .flags = AZ_ROW_DEFAULT_ON,
            .help = "Under the effect name: where the effect goes (CH SELECT 3 1 2 4, MST, MIC, A, B).",
        };
        g_row_channel = C->register_row(MOD_ID, &row);
        if (g_row_channel < 0) C->log(MOD_ID, "no Mod Menu row: the channel row stays on");
    }
    C->log(MOD_ID, "loaded on %s", C->fw_tag ? C->fw_tag() : "?");
    return 0;
}

static const char *const FW_TAGS[] = { "any", NULL };

static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI,
    .size = sizeof(az_mod_v1),
    .id = MOD_ID,
    .fw_tags = FW_TAGS,
    .init = init,
    .tick = tick,
    .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
