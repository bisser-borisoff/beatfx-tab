/* SPDX-License-Identifier: GPL-3.0-only
 * Host test for the mod's wiring: a fake core records the filters and the surface, the test
 * drives frames, touches, the view and the deck count, and checks when the label shows and
 * repaints. Built with ASan + UBSan (make unit) and with TSan (make unit-tsan). */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "azmod-mod.h"

static int g_fail, g_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_fail++;                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                \
    } while (0)

/* ---- the fake core ---- */
static unsigned g_mask;
static az_frame_fn g_ffn;
static az_touch_fn g_tfn;
static az_surface_v1 g_desc;
static int g_created, g_shown, g_redraws, g_placed_ok;
static az_rect g_place; /* the last surface_place */
static int g_view = AZ_VIEW_DECK, g_decks = 2;
static int g_paints;

static char g_log[16][160];
static int g_nlog;
static void f_log(const char *id, const char *fmt, ...)
{
    (void)id;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log[g_nlog % 16], sizeof g_log[0], fmt, ap);
    va_end(ap);
    g_nlog++;
}
static const char *last_log(void) { return g_nlog ? g_log[(g_nlog - 1) % 16] : ""; }
static const char *f_fw(void) { return "ep147-test"; }
static int f_ff(const char *id, unsigned m, az_frame_fn fn, void *u)
{
    (void)id; (void)u;
    g_mask = m;
    g_ffn = fn;
    return 0;
}
static int f_tf(const char *id, az_touch_fn fn, void *u) { (void)id; (void)u; g_tfn = fn; return 0; }
static az_surface *f_screate(const char *id, const az_surface_v1 *s)
{
    (void)id;
    g_desc = *s;
    g_created++;
    return (az_surface *)&g_desc;
}
static void f_splace(az_surface *s, az_rect r)
{
    (void)s;
    /* covers the stock tab, about x 1197-1280, y 237-282 (photos 2026-11-04); v0.1.3 is
     * 24 px wider to the left, so the name sits left of the stock tab's touch zone */
    g_placed_ok = r.x == 1106 && r.y == 233 && r.w == 174 && r.h == 52;
    g_place = r;
}
static int placed(int x, int y, int w, int h)
{
    return g_place.x == x && g_place.y == y && g_place.w == w && g_place.h == h;
}
static void f_sshow(az_surface *s, int on) { (void)s; g_shown = on; }
static void f_sredraw(az_surface *s) { (void)s; g_redraws++; }
static int f_view(void) { return g_view; }
static int f_decks(void) { return g_decks; }
static int g_arrow_rows; /* one-pixel rows at x 1255 and right: the tab layout's arrow */
static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d; (void)y; (void)w; (void)c;
    g_paints++;
    if (h == 1 && x >= 1255) g_arrow_rows++;
}
static int g_drew_one; /* a text "1" was drawn: the beat value 1 (the channel row draws "2" for CH 2) */
static char g_drew_ms[16]; /* the last text ending in "ms": TIME in the grey box */
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d; (void)x; (void)y; (void)px; (void)c;
    if (s && strcmp(s, "1") == 0) g_drew_one = 1;
    size_t n = s ? strlen(s) : 0;
    if (n > 2 && strcmp(s + n - 2, "ms") == 0) snprintf(g_drew_ms, sizeof g_drew_ms, "%s", s);
}
static int f_tw(const char *s, int px) { return (int)strlen(s) * px * 3 / 5; }

/* Mod Menu rows: the test sets g_rowv[] as the DJ would */
static az_row_v1 g_rows[4];
static char g_row_keys[4][32];
static int g_rowv[4], g_nrows;
static int f_reg_row(const char *id, const az_row_v1 *row)
{
    (void)id;
    if (g_nrows >= 4) return -1;
    g_rows[g_nrows] = *row;
    snprintf(g_row_keys[g_nrows], sizeof g_row_keys[0], "%s", row->key);
    g_rowv[g_nrows] = row->kind == AZ_ROW_CHOICE ? row->default_choice
                                                 : (row->flags & AZ_ROW_DEFAULT_ON) != 0;
    return g_nrows++;
}
static int f_row_value(int h) { return h >= 0 && h < g_nrows ? g_rowv[h] : -1; }
static int g_lookups; /* 1.1 reads no other mod's or AZ-MODS' rows */
static int f_row_lookup(const char *key) { (void)key; g_lookups++; return -1; }

/* ---- helpers ---- */
static const az_mod_v1 *M;
static unsigned char g_frame[128];

static int g_xpad;
static int g_m5d = 0x57; /* CH SELECT: MST */

static void frame(int m54, int m55, int m56, int bpm10, int val)
{
    memset(g_frame, 0, sizeof g_frame);
    g_frame[0x54] = (unsigned char)m54;
    g_frame[0x55] = (unsigned char)m55;
    g_frame[0x56] = (unsigned char)m56;
    g_frame[0x58] = (unsigned char)(bpm10 & 0xff);
    g_frame[0x59] = (unsigned char)(bpm10 >> 8);
    g_frame[0x5a] = (unsigned char)(val & 0xff);
    g_frame[0x5b] = (unsigned char)(val >> 8);
    g_frame[0x1c] = g_xpad ? 0x01 : 0x00; /* a finger on the X-PAD, and its position */
    g_frame[0x1d] = g_xpad ? 0x05 : 0xff;
    g_frame[0x5d] = (unsigned char)g_m5d;
    unsigned char copy[128];
    memcpy(copy, g_frame, sizeof copy);
    int r = g_ffn(AZ_BUS_MIXER_IN, g_frame, (int)sizeof g_frame, NULL);
    CHECK(r == AZ_FRAME_OK);
    CHECK(memcmp(copy, g_frame, sizeof copy) == 0);
}

static int g_touches_passed, g_touches;

static void touch(int x, int y)
{
    az_touch t = { 0, x, y, 1 };
    g_touches++;
    g_touches_passed += g_tfn(&t, NULL) == AZ_TOUCH_PASS;
}

/* Tick for about 300 ms, so any paced repaint goes out. */
static void settle(void)
{
    for (int i = 0; i < 15; i++) {
        M->tick();
        usleep(20 * 1000);
    }
}

static void *feeder(void *arg)
{
    (void)arg;
    unsigned char f[128];
    memset(f, 0, sizeof f);
    for (int i = 0; i < 2000; i++) {
        f[0x55] = (unsigned char)((i % 14) << 4);
        f[0x56] = 0x0a;
        f[0x58] = 0xb0, f[0x59] = 0x04; /* 120.0 BPM */
        f[0x5a] = 0xf4, f[0x5b] = 0x01; /* 500 ms */
        f[0x54] = (unsigned char)(i & 1 ? 0x81 : 0x01);
        g_ffn(AZ_BUS_MIXER_IN, f, (int)sizeof f, NULL);
        if (i % 50 == 0) usleep(1000);
    }
    return NULL;
}

int main(void)
{
    az_core_v1 core;
    memset(&core, 0, sizeof core);
    core.abi = AZ_MOD_ABI;
    core.size = sizeof core;
    core.log = f_log; core.fw_tag = f_fw;
    core.frame_filter = f_ff; core.touch_filter = f_tf;
    core.surface_create = f_screate; core.surface_place = f_splace;
    core.surface_show = f_sshow; core.surface_redraw = f_sredraw;
    core.view = f_view; core.deck_count = f_decks;
    core.fill_rect = f_fill; core.draw_text = f_text; core.text_width = f_tw;
    core.register_row = f_reg_row; core.row_value = f_row_value; core.row_lookup = f_row_lookup;

    M = az_mod_v1_entry();
    CHECK(M && M->init && M->tick);
    CHECK(M->init(&core) == 0);

    /* 1: what init registered */
    CHECK((g_mask & (1u << AZ_BUS_MIXER_IN)) != 0);
    CHECK((g_mask & AZ_FILTER_RAW) != 0);
    CHECK(g_ffn != NULL && g_tfn != NULL);
    CHECK(g_created == 1 && g_desc.views == AZ_VIEW_BIT(AZ_VIEW_DECK));
    CHECK(g_desc.layer == AZ_LAYER_OVER_EP147 && g_desc.on_paint != NULL);
    CHECK(g_placed_ok);
    CHECK(g_shown == 0);

    /* 1b: v1.1's one Mod Menu row, the channel row (on until changed). The label always sits
     * on the tab (b.sr, 2026-11-06: no top-right position), so no other row is read. */
    CHECK(g_nrows == 1);
    CHECK(strcmp(g_row_keys[0], "channel-row") == 0 && g_rows[0].kind == AZ_ROW_TOGGLE);
    CHECK((g_rows[0].flags & AZ_ROW_DEFAULT_ON) != 0);
    CHECK(g_lookups == 0);
    for (int i = 0; i < g_nrows; i++) {
        CHECK(g_rows[i].label && strlen(g_rows[i].label) <= 31);
        CHECK(g_rows[i].help && strlen(g_rows[i].help) > 0);
    }

    /* 2: no frame yet; the label sits on the tab */
    settle();
    CHECK(g_shown == 0);
    CHECK(placed(1106, 233, 174, 52));

    /* 3: a report, but the panel's state is unknown */
    frame(0x01, 0x10, 0x09, 1280, 351);
    settle();
    CHECK(g_shown == 0);

    /* 4: the handle closes the panel: the label shows and paints once */
    touch(1067, 266);
    settle();
    CHECK(g_shown == 1);
    CHECK(g_redraws == 1);

    /* 5: nothing changed */
    settle();
    CHECK(g_redraws == 1);

    /* 6: same beat at another tempo: no repaint; a new beat: one */
    frame(0x01, 0x10, 0x09, 1200, 375);
    settle();
    CHECK(g_redraws == 1);
    frame(0x01, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 2);

    /* 7: the effect sounds */
    frame(0x81, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 3);

    /* 7b: a finger on the X-PAD while it sounds changes the look (the box says X-PAD), and
     * lifting it changes it back */
    g_xpad = 1;
    frame(0x81, 0x11, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 4);
    g_xpad = 0;
    frame(0x81, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 5);

    /* 7c: another CH SELECT button repaints (0x5d); the effect's own bits in 0x54 do not */
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 5);
    g_m5d = 0x51;
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 6);

    /* 7d: turning the channel row off in the Mod Menu repaints, and on again */
    g_rowv[0] = 0;
    settle();
    CHECK(g_redraws == 7);
    g_rowv[0] = 1;
    settle();
    CHECK(g_redraws == 8);
    settle();
    CHECK(g_redraws == 8);

    /* 7d2: with the channel row off, another CH SELECT button changes nothing on the label, so
     * no repaint; turning the row back on repaints once, with the new channel */
    g_rowv[0] = 0;
    settle();
    CHECK(g_redraws == 9);
    g_m5d = 0x52;
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 9);
    g_rowv[0] = 1;
    settle();
    CHECK(g_redraws == 10);

    /* 7e: MST after a power-on (0x58) and after a press (0x57) look the same: no repaint */
    g_m5d = 0x58;
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    g_m5d = 0x57;
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_redraws == 11);

    /* 7e2: the label is drawn with the stock tab's arrow at its right end */
    g_arrow_rows = 0;
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(g_arrow_rows > 0);
    CHECK(placed(1106, 233, 174, 52) && g_shown == 1);

    /* 7f: CH 2 on a 128.0 deck. The mixer sends that BPM but keeps TIME from 120.0 (FX Probe,
     * 2026-11-06), so TIME x BPM is 1.067 beats. The label keeps BEAT 1 (1.0.0 and the first
     * 1.1 build hid it, b.sr on the deck), and paint draws what tick decided. */
    g_m5d = 0x51;
    frame(0x83, 0x10, 0x0a, 1280, 500);
    settle();
    CHECK(g_shown == 1);
    g_drew_one = 0;
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(g_drew_one == 1);
    g_m5d = 0x57;
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    CHECK(g_shown == 1);

    /* 7g: the TIME knob turned (b.sr, 2026-11-07). The beat is not confirmed, but the label
     * stays (it vanished as if the mod had crashed) with TIME in ms; turning repaints; back on
     * the beat it shows the beat again */
    frame(0x83, 0x10, 0x0a, 1200, 407);
    settle();
    CHECK(g_shown == 1);
    g_drew_ms[0] = 0;
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(strcmp(g_drew_ms, "407ms") == 0);
    int rd = g_redraws;
    frame(0x83, 0x10, 0x0a, 1200, 433);
    settle();
    CHECK(g_shown == 1 && g_redraws == rd + 1);
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(strcmp(g_drew_ms, "433ms") == 0);
    /* a fast turn: at most one repaint in MIN_REDRAW_MS (100 ms), and the last TIME lands */
    rd = g_redraws;
    for (int k = 0; k < 5; k++) {
        frame(0x83, 0x10, 0x0a, 1200, 440 + k);
        M->tick();
    }
    CHECK(g_redraws <= rd + 1);
    settle();
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(strcmp(g_drew_ms, "444ms") == 0);
    frame(0x83, 0x10, 0x0a, 1200, 500);
    settle();
    g_drew_one = 0;
    g_drew_ms[0] = 0;
    g_desc.on_paint(NULL, (az_rect){ 1106, 233, 174, 52 }, g_desc.user);
    CHECK(g_shown == 1 && g_drew_one == 1 && g_drew_ms[0] == 0);

    /* 8: the tab opens the panel; the log says why the label hid, once */
    int nl = g_nlog;
    touch(1250, 268);
    settle();
    CHECK(g_shown == 0);
    CHECK(g_nlog == nl + 1 && strstr(last_log(), "hidden") && strstr(last_log(), "panel open"));
    settle();
    CHECK(g_nlog == nl + 1);

    /* 8b: a burst of changes (handle, tab, handle, ... within a few ms) writes at most two
     * lines, and the state it ends in always lands in the log: here the panel open again */
    nl = g_nlog;
    for (int k = 0; k < 6; k++) {
        touch(k % 2 ? 1250 : 1067, k % 2 ? 268 : 266);
        M->tick();
    }
    settle();
    CHECK(g_nlog - nl <= 2);
    CHECK(strstr(last_log(), "panel open") != NULL);
    CHECK(g_shown == 0);

    /* 9: the same spot on another screen is not the handle */
    g_view = AZ_VIEW_BROWSE;
    settle();
    touch(1067, 266);
    g_view = AZ_VIEW_DECK;
    settle();
    CHECK(g_shown == 0);

    /* 10: four decks hide it, two bring it back */
    touch(1067, 266);
    settle();
    CHECK(g_shown == 1);
    g_decks = 4;
    settle();
    CHECK(g_shown == 0);
    int rs = g_redraws;
    g_decks = 2;
    settle();
    CHECK(g_shown == 1);
    CHECK(g_redraws == rs + 1); /* a fresh show paints, even with the look unchanged */

    /* 10b: a tap on the label's name is outside the stock tab's zone: the deck does not open
     * the panel, so the label stays (v0.1.2 hid it, b.sr on the deck) */
    touch(1150, 260);
    settle();
    CHECK(g_shown == 1);
    touch(1185, 260);
    settle();
    CHECK(g_shown == 1);
    /* the value box and the arrow are inside it */
    touch(1225, 260);
    settle();
    CHECK(g_shown == 0);
    touch(1067, 266);
    settle();
    CHECK(g_shown == 1);
    touch(1266, 259);
    settle();
    CHECK(g_shown == 0);
    touch(1067, 266);
    settle();
    CHECK(g_shown == 1);

    /* 11: an unknown knob position hides it */
    frame(0x01, 0xf0, 0x0a, 1200, 500);
    settle();
    CHECK(g_shown == 0);

    /* 12: every touch went on to the deck */
    CHECK(g_touches_passed == g_touches);

    /* 13: frames from another thread while ticking and painting */
    frame(0x01, 0x10, 0x0a, 1200, 500);
    pthread_t t;
    pthread_create(&t, NULL, feeder, NULL);
    for (int i = 0; i < 100; i++) {
        M->tick();
        g_desc.on_paint(NULL, (az_rect){ 1130, 225, 150, 45 }, g_desc.user);
        usleep(1000);
    }
    pthread_join(t, NULL);
    CHECK(g_paints > 0);

    printf("test_mod: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
