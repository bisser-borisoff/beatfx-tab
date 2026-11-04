/* SPDX-License-Identifier: GPL-3.0-only
 *
 * beatfx-tab: the collapsed BEAT FX tab on the 2 DECK waveform screen shows the selected
 * effect and its beat value. Spec: docs/specs/2026-11-04-beatfx-tab-design.md.
 *
 * Threads:
 *   - the mixer bus thread runs mixer_frame(): it packs the Beat FX report into g_fx;
 *   - the touch thread runs on_touch(): it follows the panel's state in g_panel;
 *   - the UI thread runs tick() (visibility, paced repaints) and the surface's paint.
 * They share only the three atomics below. The mod starts no thread of its own.
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

/* ---- UI thread ---- */

static void paint(az_draw *d, az_rect area, void *user)
{
    (void)user;
    label_paint(C, d, area, fxd_decode(atomic_load_explicit(&g_fx, memory_order_relaxed)));
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

/* What the label shows; a change in TIME or BPM alone keeps it the same. */
static int same_look(fxd_state a, fxd_state b)
{
    return a.valid == b.valid && a.effect == b.effect && a.on == b.on && a.percent == b.percent &&
           a.beat == b.beat;
}

static int s_visible;
static int s_pending;
static long s_last_redraw = -MIN_REDRAW_MS;
static fxd_state s_shown_look;

static void tick(void)
{
    int wave2 = C->view() == AZ_VIEW_DECK && C->deck_count() == 2;
    atomic_store_explicit(&g_in_wave2, wave2, memory_order_relaxed);

    fxd_state s = fxd_decode(atomic_load_explicit(&g_fx, memory_order_relaxed));
    int visible = wave2 && s.valid && atomic_load_explicit(&g_panel, memory_order_relaxed) == PT_CLOSED;
    if (visible != s_visible) {
        s_visible = visible;
        C->surface_show(g_label, visible);
        if (visible) s_pending = 1; /* a fresh show always paints the current state */
    }
    if (!visible) return;
    if (!same_look(s, s_shown_look)) s_pending = 1;

    long t = now_ms();
    if (s_pending && t - s_last_redraw >= MIN_REDRAW_MS) {
        s_pending = 0;
        s_last_redraw = t;
        s_shown_look = s;
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
