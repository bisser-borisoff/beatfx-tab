/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>

#include "geometry.h"
#include "sign_draw.h"
#include "tunables.h"

static int total, passed;
#define CHECK(c) do { total++; if (c) passed++; else printf("FAIL: %s (line %d)\n", #c, __LINE__); } while (0)

typedef struct { int x, y, w, h; uint32_t c; } fill_rec;
typedef struct { int x, y, px; uint32_t c; char s[16]; } text_rec;
static fill_rec g_fill[16];
static text_rec g_text[16];
static int g_nfill, g_ntext;

static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d;
    if (g_nfill < 16) g_fill[g_nfill] = (fill_rec){ x, y, w, h, c };
    g_nfill++;
}
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d;
    if (g_ntext < 16) {
        g_text[g_ntext] = (text_rec){ x, y, px, c, "" };
        snprintf(g_text[g_ntext].s, sizeof g_text[g_ntext].s, "%s", s);
    }
    g_ntext++;
}
static int f_text_width(const char *s, int px) { return (int)strlen(s) * px * 3 / 5; }

static az_core_v1 g_core;
static void paint(az_rect area, sign_state s)
{
    g_nfill = g_ntext = 0;
    g_core.fill_rect = f_fill;
    g_core.draw_text = f_text;
    g_core.text_width = f_text_width;
    sign_paint(&g_core, NULL, area, &s);
}
static int fill_is(int i, int x, int y, int w, int h, uint32_t c)
{
    return i < g_nfill && g_fill[i].x == x && g_fill[i].y == y && g_fill[i].w == w && g_fill[i].h == h &&
           g_fill[i].c == c;
}
static int text_is(int i, const char *s, int x, int y, int px, uint32_t c)
{
    return i < g_ntext && strcmp(g_text[i].s, s) == 0 && g_text[i].x == x && g_text[i].y == y &&
           g_text[i].px == px && g_text[i].c == c;
}

static void test_nudge(void)
{
    paint((az_rect){ 456, 259, 89, 27 }, (sign_state){ 0, 12, 12 });
    CHECK(g_nfill == 3);
    CHECK(fill_is(0, 456, 259, 89, 27, COL_BG));
    CHECK(fill_is(1, 471, 261, 5, 10, COL_TICK));
    CHECK(fill_is(2, 483, 271, 5, 10, COL_TICK));
    CHECK(g_ntext == 2);
    CHECK(text_is(0, "12", 494, 259 + NUM_DY, NUM_PX, COL_NUM));
    CHECK(text_is(1, "ms", 494 + f_text_width("12", NUM_PX) + UNIT_GAP, 259 + UNIT_DY, UNIT_PX, COL_UNIT));
}

static void test_nudge_left(void)   /* dx -4, ms 4: deck 2's tick at x 467 */
{
    paint((az_rect){ 456, 259, 89, 27 }, (sign_state){ 0, -4, 4 });
    CHECK(g_nfill == 3);
    CHECK(fill_is(0, 456, 259, 89, 27, COL_BG));
    CHECK(fill_is(1, 471, 261, 5, 10, COL_TICK));
    CHECK(fill_is(2, 467, 271, 5, 10, COL_TICK));
    CHECK(g_ntext == 2 && strcmp(g_text[0].s, "4") == 0);
    CHECK(text_is(0, "4", 494, 259 + NUM_DY, NUM_PX, COL_NUM));
    CHECK(text_is(1, "ms", 494 + f_text_width("4", NUM_PX) + UNIT_GAP, 259 + UNIT_DY, UNIT_PX, COL_UNIT));
}

static void test_locked(void)
{
    paint((az_rect){ 456, 259, 89, 27 }, (sign_state){ 1, 0, 0 });
    CHECK(g_nfill == 2);
    CHECK(fill_is(0, 456, 259, 89, 27, COL_BG));
    CHECK(fill_is(1, 471, 261, 5, 20, COL_LOCK));
    CHECK(g_ntext == 0);
}

static void test_other_box(void)    /* every x and y moves with the box */
{
    paint((az_rect){ 42, 90, 89, 27 }, (sign_state){ 0, 12, 12 });
    CHECK(g_nfill == 3);
    CHECK(fill_is(0, 42, 90, 89, 27, COL_BG));
    CHECK(fill_is(1, 57, 92, 5, 10, COL_TICK));
    CHECK(fill_is(2, 69, 102, 5, 10, COL_TICK));
    CHECK(g_ntext == 2);
    CHECK(text_is(0, "12", 80, 90 + NUM_DY, NUM_PX, COL_NUM));
    CHECK(text_is(1, "ms", 80 + f_text_width("12", NUM_PX) + UNIT_GAP, 90 + UNIT_DY, UNIT_PX, COL_UNIT));
    paint((az_rect){ 42, 90, 89, 27 }, (sign_state){ 1, 0, 0 });
    CHECK(g_nfill == 2);
    CHECK(fill_is(0, 42, 90, 89, 27, COL_BG));
    CHECK(fill_is(1, 57, 92, 5, 20, COL_LOCK));
    CHECK(g_ntext == 0);
}

static void test_three_digits_fit(void)
{
    az_rect areas[2] = { { BOX_OFF_X, BOX_OFF_Y, BOX_W, BOX_H }, { BOX_ON_X, BOX_ON_Y, BOX_W, BOX_H } };
    for (int i = 0; i < 2; i++) {
        paint(areas[i], (sign_state){ 0, DX_MAX, 240 });
        CHECK(g_ntext == 2 && strcmp(g_text[0].s, "240") == 0);
        int end = g_text[0].x + f_text_width("240", NUM_PX) + UNIT_GAP + f_text_width("ms", UNIT_PX);
        CHECK(end <= areas[i].x + BOX_W);
    }
}

static void test_same(void)
{
    sign_state a = { 1, 3, 7 }, b = { 1, 0, 0 };
    CHECK(sign_same(&a, &b));                 /* locked ignores dx and ms */
    sign_state c = { 0, 3, 7 }, d = { 0, 4, 7 }, e = { 0, 3, 8 }, f = { 0, 3, 7 };
    CHECK(!sign_same(&c, &d));
    CHECK(!sign_same(&c, &e));
    CHECK(sign_same(&c, &f));
    CHECK(!sign_same(&a, &c));                /* locked vs nudging */
}

int main(void)
{
    test_nudge();
    test_nudge_left();
    test_locked();
    test_other_box();
    test_three_digits_fit();
    test_same();
    printf("test_sign_draw: %d/%d checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
