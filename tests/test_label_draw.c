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
    return (fxd_state){ 1, effect, on, percent, beat };
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
    CHECK(background_is(COL_YELLOW));
    paint(st(11, 1, -1, "1"));
    CHECK(background_is(COL_GREEN));
    paint(st(13, 1, -1, "4"));
    CHECK(background_is(COL_PURPLE));
    CHECK(all_inside());
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
    label_paint(&g_core, NULL, R, (fxd_state){ 0, -1, 0, -1, NULL });
    CHECK(background_is(COL_OFF));
    CHECK(count_text() == 0);
    for (int i = 0; i < g_n; i++)
        CHECK(g_rec[i].rgb == COL_OFF || g_rec[i].rgb == COL_TEXT_OFF || g_rec[i].rgb == COL_CORNER);
    CHECK(g_n > 1); /* the arrow */
    CHECK(all_inside());
}

/* The left corners are rounded (radius about 4 px) by painting their outside in the screen's
 * dark ground; the right edge stays square against the screen edge, like the stock tab. */
static int has_corner_rect(int x, int y, int w)
{
    for (int i = 0; i < g_n; i++) {
        const rec_t *q = &g_rec[i];
        if (!q->text && q->rgb == COL_CORNER && q->x == x && q->y == y && q->w == w && q->h == 1)
            return 1;
    }
    return 0;
}

static void test_left_corners_are_rounded_right_ones_square(void)
{
    paint(st(1, 1, -1, "3/4"));
    CHECK(has_corner_rect(R.x, R.y, 2));
    CHECK(has_corner_rect(R.x, R.y + 1, 1));
    CHECK(has_corner_rect(R.x, R.y + R.h - 1, 2));
    CHECK(has_corner_rect(R.x, R.y + R.h - 2, 1));
    for (int i = 0; i < g_n; i++)
        if (!g_rec[i].text && g_rec[i].rgb == COL_CORNER) CHECK(g_rec[i].x + g_rec[i].w <= R.x + 2);
    CHECK(all_inside());
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
    test_longest_name_fits_before_the_box();
    test_widest_beat_fits_in_the_box();
    test_invalid_state_draws_only_the_grey_tab_and_arrow();
    test_left_corners_are_rounded_right_ones_square();
    printf("test_label_draw: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
