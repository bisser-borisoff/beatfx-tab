/* SPDX-License-Identifier: GPL-3.0-only
 * Host test for the mod's wiring: a fake core records the filters and the surface, the test
 * drives frames, touches, the view and the deck count, and checks when the label shows and
 * repaints. Built with ASan + UBSan (make unit) and with TSan (make unit-tsan). */
#include <pthread.h>
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
static int g_view = AZ_VIEW_DECK, g_decks = 2;
static int g_paints;

static void f_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }
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
}
static void f_sshow(az_surface *s, int on) { (void)s; g_shown = on; }
static void f_sredraw(az_surface *s) { (void)s; g_redraws++; }
static int f_view(void) { return g_view; }
static int f_decks(void) { return g_decks; }
static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t c) { (void)d; (void)x; (void)y; (void)w; (void)h; (void)c; g_paints++; }
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c) { (void)d; (void)x; (void)y; (void)s; (void)px; (void)c; }
static int f_tw(const char *s, int px) { return (int)strlen(s) * px * 3 / 5; }

/* ---- helpers ---- */
static const az_mod_v1 *M;
static unsigned char g_frame[128];

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

    /* 2: no frame yet */
    settle();
    CHECK(g_shown == 0);

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

    /* 8: the tab opens the panel */
    touch(1250, 268);
    settle();
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
    g_decks = 2;
    settle();
    CHECK(g_shown == 1);

    /* 10b: a tap on the label's name is outside the stock tab's zone: the deck does not open
     * the panel, so the label stays (v0.1.2 hid it, Бисер on the deck) */
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
