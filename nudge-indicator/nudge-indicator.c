/* SPDX-License-Identifier: GPL-3.0-only
 *
 * nudge-indicator: on the 2 DECK screen a small sign shows how far apart in milliseconds the beat
 * phases of decks 1 and 2 are while both play at the same tempo (two ticks that slide against each
 * other and the difference as a number), and a green line when they are where Sync would put them.
 * The decisions are in phase.c (the difference, the gate, the lock state machine), the picture is
 * in sign_draw.c. This file is the shell: services, one surface, the Mod Menu rows, the Status page.
 *
 * Threads, and what they share:
 *   - the UI thread runs tick() and the Status page (on_enter, on_draw, the row callbacks). It
 *     reads the decks, runs pl_step, places, shows, hides and repaints the surface. Everything it
 *     owns is a plain static (s_*) that no other thread touches.
 *   - the overlay thread runs paint(): it copies g_sign under g_mu and draws it.
 * g_mu guards g_sign (what paint draws) and nothing else; it is held only to copy, never across a
 * draw or a core call. The mod has no threads of its own, no hooks, no frames, no files.
 *
 * Display only: it reads deck_info and deck_grid, and draws one surface.
 */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"
#include "geometry.h"
#include "phase.h"
#include "sign_draw.h"
#include "tunables.h"

#define MOD_ID "nudge-indicator"
#define SIDES 2           /* deck 1 and deck 2 */
#define DECKS 4           /* deck_info is read for all four: 3 and 4 only to see whether they are in use */
#define FIRST_EXTRA_DECK 2
#define MS_PER_S 1000L
#define NS_PER_MS 1000000L
#define PAGE_LINE_MAX 200
#define UTF8_CONT_MASK 0xC0 /* a UTF-8 continuation byte is 10xxxxxx */
#define UTF8_CONT 0x80

static const az_core_v1 *C;

/* ---- what the threads share, under g_mu ------------------------------------------------------ */

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static sign_state g_sign; /* what paint draws; written by tick, before it shows or repaints */

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * MS_PER_S + ts.tv_nsec / NS_PER_MS;
}

/* ---- the surface (overlay thread) ------------------------------------------------------------- */

static az_surface *g_surf;

static void paint(az_draw *d, az_rect a, void *user)
{
    (void)user;
    sign_state s;
    pthread_mutex_lock(&g_mu);
    s = g_sign;
    pthread_mutex_unlock(&g_mu);
    sign_paint(C, d, a, &s);
}

static const az_surface_v1 SURF = {
    sizeof(az_surface_v1), AZ_LAYER_OVER_EP147, AZ_VIEW_BIT(AZ_VIEW_DECK), paint, NULL, NULL,
};
static const az_rect BOX_OFF = { BOX_OFF_X, BOX_OFF_Y, BOX_W, BOX_H };
static const az_rect BOX_ON = { BOX_ON_X, BOX_ON_Y, BOX_W, BOX_H };

/* ---- the Mod Menu rows ------------------------------------------------------------------------- */

static int g_row_enabled = -1; /* our toggle; -1: no row, always on */
static int g_row_pm = -1;      /* the official Phase Meter row; -1: not found, taken as off */

static int enabled_now(void)
{
    if (g_row_enabled < 0 || !AZ_HAS(C, row_value)) return 1;
    return C->row_value(g_row_enabled) != 0;
}

static int phase_meter_on(void)
{
    if (!AZ_HAS(C, row_lookup) || !AZ_HAS(C, row_value) || g_row_pm < 0) return 0;
    return C->row_value(g_row_pm) == 1;
}

/* ---- tick (UI thread) -------------------------------------------------------------------------- */

static pl_sm s_sm;
static pl_in s_in;                 /* the decks as the last tick read them */
static pl_out s_out;               /* what the last pl_step decided */
static int s_shown;                /* the surface is shown */
static int s_mode;                 /* 1: Phase Meter on, the box is at BOX_ON */
static int s_drawn_ok;             /* s_drawn is what the surface shows now */
static sign_state s_drawn;         /* the last picture handed to paint */
static pl_state s_drawn_pstate;    /* pl_state at that paint */
static uint32_t s_drawn_t;         /* the clock at that paint */

static void read_decks(pl_in *in, uint32_t t32)
{
    memset(in, 0, sizeof *in);
    in->t_ms = t32;
    in->row_on = enabled_now();
    in->theme = C->ui_theme() != 0;
    in->deck_count = C->deck_count();
    az_deck_info di[DECKS];
    for (int i = 0; i < DECKS; i++) {
        memset(&di[i], 0, sizeof di[i]);
        di[i].size = sizeof di[i];
        if (C->deck_info(i, &di[i]) != 0) di[i].loaded = 0;
    }
    in->deck34 = di[FIRST_EXTRA_DECK].loaded == 1 || di[FIRST_EXTRA_DECK + 1].loaded == 1;
    for (int i = 0; i < SIDES; i++) {
        double bar = 0.0, ph = 0.0;
        in->loaded[i] = di[i].loaded == 1;
        in->playing[i] = di[i].playing == 1;
        in->bpm[i] = di[i].bpm;
        in->grid_ok[i] = C->deck_grid(i, &ph, &bar) == 0;
        in->ph[i] = ph;
    }
}

static int s_page_open;   /* the Status page drew, or was entered, at s_page_t (UI thread) */
static long s_page_t;
static long s_page_req;   /* when tick last asked for a repaint of it */

/* The page's live lines: a repaint at most every PAGE_REFRESH_MS, and only while the page is being
 * drawn. The SDK has no callback for leaving a page, so "open" means it drew (or was entered)
 * within PAGE_ALIVE_MS; a closed page stops being drawn and the requests stop with it. */
static void page_refresh(long t)
{
    if (s_page_open && t - s_page_t < PAGE_ALIVE_MS && t - s_page_req >= PAGE_REFRESH_MS) {
        s_page_req = t;
        C->request_redraw();
    }
}

static void tick(void)
{
    const long t = now_ms();
    const uint32_t t32 = (uint32_t)t;

    read_decks(&s_in, t32);
    pl_step(&s_sm, &s_in, &s_out);
    const int want = s_out.state != PL_HIDDEN && C->view() == AZ_VIEW_DECK;

    int mode = phase_meter_on();
    if (mode != s_mode) { /* the Phase Meter was switched: the box moves in this tick and is painted anew */
        s_mode = mode;
        C->surface_place(g_surf, mode ? BOX_ON : BOX_OFF);
        s_drawn_ok = 0;
    }

    sign_state ns;
    ns.locked = s_out.state == PL_LOCKED;
    ns.dx = s_out.dx;
    ns.ms = s_out.ms > MS_MAX ? MS_MAX : s_out.ms;

    int draw = 0;
    if (want) {
        draw = !s_drawn_ok || !sign_same(&ns, &s_drawn);
        /* nudging repaints at most every NUDGE_REDRAW_MS; a change of state or of mode at once */
        if (draw && s_drawn_ok && s_out.state == PL_NUDGE && s_drawn_pstate == PL_NUDGE &&
            (uint32_t)(t32 - s_drawn_t) < NUDGE_REDRAW_MS)
            draw = 0;
    }
    if (draw) { /* what paint draws is in place before the surface shows or repaints */
        pthread_mutex_lock(&g_mu);
        g_sign = ns;
        pthread_mutex_unlock(&g_mu);
        s_drawn = ns;
        s_drawn_ok = 1;
        s_drawn_pstate = s_out.state;
        s_drawn_t = t32;
    }
    if (want != s_shown) {
        s_shown = want;
        C->surface_show(g_surf, want);
        if (!want) s_drawn_ok = 0; /* paint again when it comes back */
    }
    if (draw) C->surface_redraw(g_surf);
    page_refresh(t);
}

#ifdef PL_TEST
/* tests only (UI thread): the last pl_step result; returns 1 while the surface is shown */
int pl_test_out(pl_out *o)
{
    *o = s_out;
    return s_shown;
}

/* tests only (UI thread): what paint would draw now; returns 1 while the surface is shown */
int pl_test_sign(sign_state *s)
{
    pthread_mutex_lock(&g_mu);
    *s = g_sign;
    pthread_mutex_unlock(&g_mu);
    return s_shown;
}
#endif

/* ---- the Status page (UI thread) ----------------------------------------------------------------- */

/* a deck reading the gate counts as usable (the same tests as phase.c's gate) */
static int ph_valid(double ph) { return isfinite(ph) && ph >= 0.0 && ph <= 1.0; }
static int bpm_valid(double bpm) { return isfinite(bpm) && bpm > 0.0; }

/* One deck, as the last tick read it: the first thing that is wrong with it, else its numbers. */
static void deck_line(int side, char *out, size_t n)
{
    const pl_in *in = &s_in;
    if (!in->loaded[side]) snprintf(out, n, "Deck %d: no track", side + 1);
    else if (!in->playing[side]) snprintf(out, n, "Deck %d: stopped", side + 1);
    else if (!in->grid_ok[side] || !ph_valid(in->ph[side])) snprintf(out, n, "Deck %d: no beat grid", side + 1);
    else if (!bpm_valid(in->bpm[side])) snprintf(out, n, "Deck %d: tempo unknown", side + 1);
    else snprintf(out, n, "Deck %d: plays, %.1f BPM, phase %.2f", side + 1, in->bpm[side], in->ph[side]);
}

static void diff_line(char *out, size_t n)
{
    if (!s_out.have_delta) snprintf(out, n, "Difference: -");
    else if (fabs(s_out.delta_ms) < IN_PHASE_MS) snprintf(out, n, "Difference: in phase (%.1f ms)", fabs(s_out.delta_ms));
    else snprintf(out, n, "Difference: deck %d ahead by %.1f ms", s_out.delta_ms > 0.0 ? 1 : 2, fabs(s_out.delta_ms));
}

/* The reason the sign is not "locked" (pl_why), in words. */
static const char *why_text(const pl_out *o, const double bpm[2], char *buf, size_t n)
{
    switch (o->why) {
    case PL_ROW_OFF: snprintf(buf, n, "Nudge Indicator is off"); break;
    case PL_THEME: snprintf(buf, n, "a theme is on"); break;
    case PL_NOT_2DECK: snprintf(buf, n, "not the 2 DECK view"); break;
    case PL_DECK34: snprintf(buf, n, "deck 3 or 4 has a track"); break;
    case PL_NO_TRACK_1: snprintf(buf, n, "deck 1 has no track"); break;
    case PL_NO_TRACK_2: snprintf(buf, n, "deck 2 has no track"); break;
    case PL_STOPPED_1: snprintf(buf, n, "deck 1 is stopped"); break;
    case PL_STOPPED_2: snprintf(buf, n, "deck 2 is stopped"); break;
    case PL_NO_GRID_1: snprintf(buf, n, "deck 1 has no beat grid"); break;
    case PL_NO_GRID_2: snprintf(buf, n, "deck 2 has no beat grid"); break;
    case PL_NO_TEMPO_1: snprintf(buf, n, "deck 1's tempo is unknown"); break;
    case PL_NO_TEMPO_2: snprintf(buf, n, "deck 2's tempo is unknown"); break;
    case PL_TEMPO_DIFF: snprintf(buf, n, "tempos differ (%.1f / %.1f)", bpm[0], bpm[1]); break;
    case PL_APART: snprintf(buf, n, "%d ms apart", o->ms); break;
    case PL_HOLDING: snprintf(buf, n, "settling"); break;
    case PL_OK: snprintf(buf, n, "?"); break;
    }
    return buf;
}

static void lock_line(char *out, size_t n)
{
    if (s_out.state == PL_LOCKED) {
        snprintf(out, n, "In phase: yes");
        return;
    }
    char why[PAGE_LINE_MAX];
    snprintf(out, n, "In phase: no, %s", why_text(&s_out, s_in.bpm, why, sizeof why));
}

/* One line of the page, cut short (at a whole character) to fit; nothing below the area. */
static void put_line(az_draw *d, az_rect a, int *y, const char *s)
{
    char buf[PAGE_LINE_MAX];
    const int x = a.x + PAGE_X, maxw = a.x + a.w - x - PAGE_RIGHT;
    if (*y + PAGE_LINE_H > a.y + a.h) return;
    snprintf(buf, sizeof buf, "%s", s);
    size_t len = strlen(buf);
    while (len > 0 && C->text_width(buf, PAGE_PX) > maxw) {
        do len--;
        while (len > 0 && ((unsigned char)buf[len] & UTF8_CONT_MASK) == UTF8_CONT);
        buf[len] = 0;
    }
    if (len) C->draw_text(d, x, *y, buf, PAGE_PX, C->color(AZ_COL_TEXT));
    *y += PAGE_LINE_H;
}

static void on_enter(void *user)
{
    (void)user;
    s_page_open = 1;
    s_page_t = now_ms(); /* the page is open: tick starts the repaints */
}

static void on_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    s_page_open = 1;
    s_page_t = now_ms();
    C->draw_header(d, "Nudge Indicator");
    int y = a.y + C->row_height(d) + PAGE_TOP;
    char line[PAGE_LINE_MAX];
    for (int side = 0; side < SIDES; side++) {
        deck_line(side, line, sizeof line);
        put_line(d, a, &y, line);
    }
    diff_line(line, sizeof line);
    put_line(d, a, &y, line);
    lock_line(line, sizeof line);
    put_line(d, a, &y, line);
    put_line(d, a, &y, s_mode ? "Place: left of the Phase Meter (Phase Meter on)"
                              : "Place: right of the loop sign (Phase Meter off)");
}

static const az_screen_v1 PAGE = {
    .size = sizeof(az_screen_v1),
    .on_draw = on_draw,
    .on_enter = on_enter,
    .user = NULL,
};

/* ---- lifecycle ----------------------------------------------------------------------------------- */

static void register_rows(void)
{
    az_row_v1 row;
    memset(&row, 0, sizeof row);
    row.size = sizeof row;
    row.key = "enabled";
    row.label = "Nudge Indicator";
    row.kind = AZ_ROW_TOGGLE;
    row.flags = AZ_ROW_DEFAULT_ON;
    row.help = "Shows how far apart the two decks' beats are, and a green line when they are in phase.";
    g_row_enabled = C->register_row(MOD_ID, &row); /* -1: the sign stays on */

    memset(&row, 0, sizeof row);
    row.size = sizeof row;
    row.key = "status";
    row.label = "Status";
    row.kind = AZ_ROW_SCREEN;
    row.help = "What the mod sees on the two decks.";
    row.screen = &PAGE;
    if (C->register_row(MOD_ID, &row) < 0) C->log(MOD_ID, "no Status row (register_row refused)");
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_show) ||
        !AZ_HAS(C, surface_redraw) || !AZ_HAS(C, view) || !AZ_HAS(C, deck_count) || !AZ_HAS(C, ui_theme) ||
        !AZ_HAS(C, deck_info) || !AZ_HAS(C, deck_grid)) {
        C->log(MOD_ID, "this core lacks a service nudge-indicator needs (AZ-MODS 1.2.3 or newer)");
        return 2;
    }
    if (AZ_HAS(C, row_lookup)) g_row_pm = C->row_lookup("phase-meter");
    else C->log(MOD_ID, "no row_lookup: the Phase Meter is taken as off");
    int rows = AZ_HAS(C, register_row) && AZ_HAS(C, row_value);
    if (rows) register_rows();
    else C->log(MOD_ID, "no Mod Menu rows: always on");
    g_surf = C->surface_create(MOD_ID, &SURF);
    if (!g_surf) return 3; /* the core logged why */
    C->surface_show(g_surf, 0);
    C->surface_place(g_surf, BOX_OFF);
    if (!rows && AZ_HAS(C, register_screen) && C->register_screen(MOD_ID, &PAGE) != 0)
        C->log(MOD_ID, "no Mod Menu page (register_screen refused)");
    C->log(MOD_ID, "loaded");
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
};

const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
