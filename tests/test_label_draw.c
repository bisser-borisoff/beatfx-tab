/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for label_draw: a fake core records every fill_rect and draw_text. */
#include <stdio.h>
#include <string.h>

#include "geometry.h"
#include "label_draw.h"

static int g_fail, g_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_fail++;                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                \
    } while (0)

typedef struct {
    int text; /* 0 = fill_rect, 1 = draw_text */
    int x, y, w, h, px;
    uint32_t rgb;
    char s[48];
} rec_t;

static rec_t g_rec[256];
static int g_n;

static int f_text_width(const char *s, int px) { return (int)strlen(s) * px * 3 / 5; }

static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t rgb)
{
    (void)d;
    if (g_n < 256) g_rec[g_n++] = (rec_t){ 0, x, y, w, h, 0, rgb, "" };
}

static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t rgb)
{
    (void)d;
    if (g_n >= 256) return;
    rec_t r = { 1, x, y, f_text_width(s, px), px * 3 / 2, px, rgb, "" };
    snprintf(r.s, sizeof r.s, "%s", s);
    g_rec[g_n++] = r;
}

static az_core_v1 g_core;
static const az_rect R = { LABEL_X, LABEL_Y, LABEL_W, LABEL_H };

static void paint(fxd_state s)
{
    g_n = 0;
    label_paint(&g_core, NULL, R, s);
}

static fxd_state st(int effect, int on, int percent, const char *beat)
{
    return (fxd_state){ 1, effect, on, percent, beat, 0 };
}

static const rec_t *find_text(const char *s)
{
    for (int i = 0; i < g_n; i++)
        if (g_rec[i].text && strcmp(g_rec[i].s, s) == 0) return &g_rec[i];
    return NULL;
}

static const rec_t *find_fill(uint32_t rgb)
{
    for (int i = 0; i < g_n; i++)
        if (!g_rec[i].text && g_rec[i].rgb == rgb) return &g_rec[i];
    return NULL;
}

static int count_text(void)
{
    int n = 0;
    for (int i = 0; i < g_n; i++) n += g_rec[i].text;
    return n;
}

/* Every rectangle and every text box lies wholly inside the label. */
static int all_inside(void)
{
    for (int i = 0; i < g_n; i++) {
        const rec_t *q = &g_rec[i];
        if (q->x < R.x || q->y < R.y || q->x + q->w > R.x + R.w || q->y + q->h > R.y + R.h) {
            printf("  outside: %s %s at %d,%d %dx%d\n", q->text ? "text" : "rect", q->s, q->x, q->y,
                   q->w, q->h);
            return 0;
        }
    }
    return 1;
}

static int background_is(uint32_t rgb)
{
    return g_n > 0 && !g_rec[0].text && g_rec[0].rgb == rgb && g_rec[0].x == R.x &&
           g_rec[0].y == R.y && g_rec[0].w == R.w && g_rec[0].h == R.h;
}

static void test_echo_off_is_grey_with_white_name_and_beat_box(void)
{
    paint(st(1, 0, -1, "3/4"));
    CHECK(background_is(COL_OFF));
    const rec_t *name = find_text("ECHO"), *beat = find_text("3/4"), *box = find_fill(COL_BOX);
    CHECK(name && name->rgb == COL_TEXT_OFF);
    CHECK(beat && beat->rgb == COL_BOX_TEXT);
    CHECK(box && box->x + box->w <= R.x + R.w && box->x >= R.x + R.w / 2);
    CHECK(beat && box && beat->x >= box->x && beat->x + beat->w <= box->x + box->w);
    CHECK(all_inside());
}

static void test_sounding_effects_take_their_group_colour(void)
{
    paint(st(1, 1, -1, "3/4"));
    CHECK(background_is(COL_BLUE));
    const rec_t *name = find_text("ECHO");
    CHECK(name && name->rgb == COL_TEXT_ON);
    CHECK(all_inside());

    paint(st(5, 1, 50, NULL));
    CHECK(background_is(COL_ORANGE));
    CHECK(find_text("50%") != NULL);
    CHECK(find_text("REVERB") != NULL);
    CHECK(all_inside());

    paint(st(10, 1, -1, "1"));
    CHECK(background_is(COL_LIME));
    paint(st(11, 1, -1, "1"));
    CHECK(background_is(COL_GREEN));
    paint(st(13, 1, -1, "4"));
    CHECK(background_is(COL_PURPLE));
    CHECK(all_inside());
}

/* The stock BEAT FX panel's X-PAD colours, matched on the deck against swatches beside the open
 * panel (b.sr, 2026-11-04): one sounding effect from each group, as literal values so a
 * changed constant shows up here. */
static void test_groups_take_the_stock_panels_colours(void)
{
    paint(st(4, 1, -1, "1")); /* HELIX: blue */
    CHECK(background_is(0x28a0ff));
    paint(st(5, 1, 50, NULL)); /* REVERB: orange */
    CHECK(background_is(0xf07828));
    paint(st(10, 1, -1, "1")); /* TRANS: lime, like FLANGER, PHASER, FILTER, TRIPLET FILTER */
    CHECK(background_is(0x98d02c));
    paint(st(12, 1, -1, "1/3")); /* TRIPLET ROLL: green, like ROLL */
    CHECK(background_is(0x3c9820));
    paint(st(13, 1, -1, "4")); /* MOBIUS: purple */
    CHECK(background_is(0xc050ff));
}

static void test_longest_name_fits_before_the_box(void)
{
    paint(st(9, 1, -1, "4/3"));
    const rec_t *name = find_text("TRIPLET FILTER");
    if (!name) name = find_text("TRI FILTER");
    const rec_t *box = find_fill(COL_BOX);
    CHECK(name != NULL && box != NULL);
    CHECK(name && box && name->x + name->w <= box->x);
    CHECK(name && name->px >= 12);
    CHECK(find_text("4/3") != NULL);
    CHECK(all_inside());

    paint(st(12, 0, -1, "1/3"));
    CHECK(find_text("TRIPLET ROLL") || find_text("TRI ROLL"));
    CHECK(all_inside());
}

static void test_widest_beat_fits_in_the_box(void)
{
    paint(st(1, 0, -1, "16/3"));
    const rec_t *beat = find_text("16/3"), *box = find_fill(COL_BOX);
    CHECK(beat && box && beat->x >= box->x && beat->x + beat->w <= box->x + box->w);
    CHECK(all_inside());
}

static void test_invalid_state_draws_only_the_grey_tab_and_arrow(void)
{
    g_n = 0;
    label_paint(&g_core, NULL, R, (fxd_state){ 0, -1, 0, -1, NULL, 0 });
    CHECK(background_is(COL_OFF));
    CHECK(count_text() == 0);
    for (int i = 0; i < g_n; i++) CHECK(g_rec[i].rgb == COL_OFF || g_rec[i].rgb == COL_TEXT_OFF);
    CHECK(g_n > 1); /* the arrow */
    CHECK(all_inside());
}

/* v0.1.2: the corners are square. The rounded ones (v0.1.1) were cut in black, which showed
 * as a dead pixel wherever a light part of the waveform ran under the label (b.sr, deck). */
static int painted_over(int x, int y)
{
    for (int i = 1; i < g_n; i++) { /* g_rec[0] is the background */
        const rec_t *q = &g_rec[i];
        if (!q->text && x >= q->x && x < q->x + q->w && y >= q->y && y < q->y + q->h) return 1;
    }
    return 0;
}

static void test_corners_are_square(void)
{
    const fxd_state cases[] = { st(1, 1, -1, "3/4"), st(1, 0, -1, "3/4"),
                                (fxd_state){ 0, -1, 0, -1, NULL, 0 } };
    for (int c = 0; c < 3; c++) {
        paint(cases[c]);
        CHECK(!painted_over(R.x, R.y));
        CHECK(!painted_over(R.x + 1, R.y + 1));
        CHECK(!painted_over(R.x, R.y + R.h - 1));
        CHECK(!painted_over(R.x + 1, R.y + R.h - 2));
        CHECK(!painted_over(R.x + R.w - 1, R.y));
        CHECK(!painted_over(R.x + R.w - 1, R.y + R.h - 1));
    }
}

/* v0.1.2: while the effect is off, the name is underlined in its group's X-PAD colour, a
 * UNDERLINE_H (4 px) line as wide as the drawn name, just below the letters. */
static const rec_t *underline(uint32_t rgb)
{
    for (int i = 1; i < g_n; i++)
        if (!g_rec[i].text && g_rec[i].rgb == rgb) return &g_rec[i];
    return NULL;
}

static void check_underlined(const char *name, uint32_t rgb)
{
    const rec_t *n = find_text(name), *u = underline(rgb), *box = find_fill(COL_BOX);
    CHECK(n != NULL && u != NULL && box != NULL);
    if (!n || !u || !box) return;
    CHECK(u->h == UNDERLINE_H);
    CHECK(u->x == n->x && u->w == n->w);
    CHECK(u->y >= n->y + n->h / 2 + n->px / 3); /* below the letters, which centre in n's box */
    CHECK(u->y <= n->y + n->h / 2 + n->px / 2 + UNDERLINE_GAP + 1); /* and close under them */
    CHECK(u->x + u->w <= box->x);
    CHECK(all_inside());
}

static void test_off_effect_name_is_underlined_in_its_group_colour(void)
{
    paint(st(0, 0, -1, "1/2"));
    CHECK(background_is(COL_OFF));
    check_underlined("DELAY", COL_BLUE);
    paint(st(5, 0, 40, NULL));
    check_underlined("REVERB", COL_ORANGE);
    paint(st(6, 0, -1, "1"));
    check_underlined("FLANGER", COL_LIME);
    paint(st(11, 0, -1, "1/4"));
    check_underlined("ROLL", COL_GREEN);
    paint(st(13, 0, -1, "2"));
    check_underlined("MOBIUS", COL_PURPLE);
}

static void test_underline_follows_a_shrunk_name(void)
{
    paint(st(9, 0, -1, "4/3"));
    const rec_t *n = find_text("TRIPLET FILTER");
    if (!n) n = find_text("TRI FILTER");
    CHECK(n != NULL);
    if (n) check_underlined(n->s, COL_LIME);
}

static void test_on_effect_has_no_underline(void)
{
    const int fx[] = { 0, 5, 6, 11, 13 };
    const uint32_t col[] = { COL_BLUE, COL_ORANGE, COL_LIME, COL_GREEN, COL_PURPLE };
    for (int i = 0; i < 5; i++) {
        paint(st(fx[i], 1, 40, fx[i] == 5 ? NULL : "1"));
        CHECK(background_is(col[i]));
        CHECK(underline(col[i]) == NULL);
    }
}

/* v0.1.3: only the stock tab's zone (TAB_X0 on) opens the panel, so the arrow and the value
 * box sit inside it and the name left of it, ZONE_MARGIN pixels clear of its edge either side
 * (the edge is measured from photos). The arrow moves to the label's right end. */
static int arrow_extent(uint32_t ink, int *x0, int *x1)
{
    int found = 0;
    for (int i = 1; i < g_n; i++) {
        const rec_t *q = &g_rec[i];
        if (q->text || q->rgb != ink || q->h != 1) continue;
        if (!found || q->x < *x0) *x0 = q->x;
        if (!found || q->x + q->w > *x1) *x1 = q->x + q->w;
        found = 1;
    }
    return found;
}

static void test_arrow_and_box_sit_in_the_stock_tab_zone(void)
{
    const fxd_state cases[] = { st(1, 0, -1, "3/4"), st(1, 1, -1, "3/4"),
                                (fxd_state){ 0, -1, 0, -1, NULL, 0 } };
    const uint32_t ink[] = { COL_TEXT_OFF, COL_TEXT_ON, COL_TEXT_OFF };
    for (int c = 0; c < 3; c++) {
        paint(cases[c]);
        int x0 = 0, x1 = 0;
        CHECK(arrow_extent(ink[c], &x0, &x1));
        CHECK(x0 >= TAB_X0 + ZONE_MARGIN);
        CHECK(x1 <= R.x + R.w - 6);
        const rec_t *box = find_fill(COL_BOX);
        if (c < 2) {
            CHECK(box && box->x >= TAB_X0 + ZONE_MARGIN && box->x + box->w <= x0);
        } else {
            CHECK(box == NULL);
        }
        CHECK(all_inside());
    }
}

static void test_names_stay_left_of_the_stock_tab_zone(void)
{
    for (int e = 0; e < 14; e++) {
        for (int on = 0; on < 2; on++) {
            paint(e == 5 ? st(e, on, 100, NULL) : st(e, on, -1, "16/3"));
            const rec_t *box = find_fill(COL_BOX);
            int names = 0;
            for (int i = 0; i < g_n; i++) {
                const rec_t *q = &g_rec[i];
                if (!q->text || !box || q->x >= box->x) continue;
                names++;
                CHECK(q->x >= R.x + 4);
                CHECK(q->x + q->w <= TAB_X0 - ZONE_MARGIN);
                CHECK(q->px >= 12 && strlen(q->s) >= 4);
            }
            CHECK(names == 1);
            CHECK(all_inside());
        }
    }
}

/* v0.1.4: while a finger is on the X-PAD, the box says "X-PAD" (b.sr's pick from a mockup),
 * since the beat that sounds is then the pad's, not the BEAT value. */
static void test_xpad_shows_in_the_box(void)
{
    fxd_state s = st(1, 1, -1, "1");
    s.xpad = 1;
    paint(s);
    CHECK(background_is(COL_BLUE));
    const rec_t *v = find_text("X-PAD"), *box = find_fill(COL_BOX);
    CHECK(v && box && v->x >= box->x && v->x + v->w <= box->x + box->w);
    CHECK(v && v->rgb == COL_BOX_TEXT && v->px >= 12);
    CHECK(find_text("1") == NULL);
    CHECK(find_text("ECHO") != NULL);
    CHECK(all_inside());

    s = st(5, 1, 40, NULL);
    s.xpad = 1;
    paint(s);
    CHECK(find_text("X-PAD") != NULL && find_text("40%") == NULL);

    s = st(1, 0, -1, "1"); /* silent: the BEAT value, whatever the flag says */
    s.xpad = 1;
    paint(s);
    CHECK(find_text("1") != NULL && find_text("X-PAD") == NULL);
}

int main(void)
{
    g_core.abi = AZ_MOD_ABI;
    g_core.size = sizeof g_core;
    g_core.fill_rect = f_fill;
    g_core.draw_text = f_text;
    g_core.text_width = f_text_width;

    test_echo_off_is_grey_with_white_name_and_beat_box();
    test_sounding_effects_take_their_group_colour();
    test_groups_take_the_stock_panels_colours();
    test_longest_name_fits_before_the_box();
    test_widest_beat_fits_in_the_box();
    test_invalid_state_draws_only_the_grey_tab_and_arrow();
    test_corners_are_square();
    test_off_effect_name_is_underlined_in_its_group_colour();
    test_underline_follows_a_shrunk_name();
    test_on_effect_has_no_underline();
    test_arrow_and_box_sit_in_the_stock_tab_zone();
    test_names_stay_left_of_the_stock_tab_zone();
    test_xpad_shows_in_the_box();
    printf("test_label_draw: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
