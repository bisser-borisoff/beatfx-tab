/* SPDX-License-Identifier: GPL-3.0-only */
#include "label_draw.h"

#include <stdio.h>
#include <string.h>

#include "geometry.h"

/* Layout. The name starts NAME_X from the label's left edge and ends ZONE_MARGIN short of the
 * stock tab's touch zone (TAB_X0); the value box starts ZONE_MARGIN inside it; the arrow's tip
 * is ARROW_RIGHT from the label's right edge, in the dark strip right of the waveform. */
#define ARROW_RIGHT 17
#define ARROW_HALF 6 /* rows above and below the tip */
#define BOX_W 46
#define BOX_VPAD 7
#define NAME_PX_MAX 18
#define BEAT_PX_MAX 20
#define PX_MIN 12

static const uint32_t GROUP_COL[] = { COL_BLUE, COL_ORANGE, COL_LIME, COL_GREEN, COL_PURPLE };

static int width(const az_core_v1 *C, const char *s, int px)
{
    return C->text_width ? C->text_width(s, px) : (int)strlen(s) * px;
}

/* The largest size from max down to PX_MIN at which s fits in w; 0 if none does. */
static int fit_px(const az_core_v1 *C, const char *s, int max, int w)
{
    for (int px = max; px >= PX_MIN; px--)
        if (width(C, s, px) <= w) return px;
    return 0;
}

/* A left-pointing triangle, one fill_rect per row. */
static void arrow(const az_core_v1 *C, az_draw *d, az_rect r, uint32_t rgb)
{
    int x0 = r.x + r.w - ARROW_RIGHT, cy = r.y + r.h / 2;
    for (int k = -ARROW_HALF; k <= ARROW_HALF; k++) {
        int a = k < 0 ? -k : k;
        C->fill_rect(d, x0 + a, cy + k, ARROW_HALF + 1 - a, 1, rgb);
    }
}

/* A round dot px wide whose rows all cover column cx, one fill_rect per row: a pixel is in
 * when its centre lies within the circle (in half-pixel units, no floating point). */
static void dot(const az_core_v1 *C, az_draw *d, int cx, int cy, int px, uint32_t rgb)
{
    int x0 = cx - px / 2, y0 = cy - px / 2;
    for (int j = 0; j < px; j++) {
        int dy = 2 * j + 1 - px, lim = px * px - dy * dy, a = -1, b = -1;
        for (int i = 0; i < px; i++) {
            int dx = 2 * i + 1 - px;
            if (dx * dx <= lim) {
                if (a < 0) a = i;
                b = i;
            }
        }
        if (a >= 0) C->fill_rect(d, x0 + a, y0 + j, b - a + 1, 1, rgb);
    }
}

static void text_vcentred(const az_core_v1 *C, az_draw *d, int x, int top, int h, const char *s,
                          int px, uint32_t rgb)
{
    C->draw_text(d, x, top + (h - px * 3 / 2) / 2, s, px, rgb);
}

static void text_centred(const az_core_v1 *C, az_draw *d, int cx, int cy, const char *s,
                         uint32_t rgb)
{
    C->draw_text(d, cx - width(C, s, DOT_TEXT_PX) / 2, cy - DOT_TEXT_PX * 3 / 4, s, DOT_TEXT_PX,
                 rgb);
}

static uint32_t dimmed(uint32_t rgb) /* each channel at 70% */
{
    return ((rgb >> 16 & 0xff) * 7 / 10) << 16 | ((rgb >> 8 & 0xff) * 7 / 10) << 8 |
           (rgb & 0xff) * 7 / 10;
}

/* The channel row: slots for CH SELECT 3 1 2 4, the selected channel as its digit, the others
 * as dim dots; MST or MIC over all four, A over the left pair, B over the right pair. */
static void channel_row(const az_core_v1 *C, az_draw *d, az_rect r, int cy, fxd_state s,
                        uint32_t lit, uint32_t dim)
{
    static const int ORDER[4] = { FXD_CH_3, FXD_CH_1, FXD_CH_2, FXD_CH_4 };
    static const char *const DIGIT[4] = { "3", "1", "2", "4" };
#define SLOT(i) (r.x + DOT_SLOT0_CX + (i) * DOT_PITCH)
    if (s.ch == FXD_CH_MST || s.ch == FXD_CH_MIC) {
        text_centred(C, d, (SLOT(0) + SLOT(3)) / 2, cy, s.ch == FXD_CH_MST ? "MST" : "MIC", lit);
    } else {
        int a = s.ch == FXD_CH_CFA, b = s.ch == FXD_CH_CFB;
        if (a) text_centred(C, d, (SLOT(0) + SLOT(1)) / 2, cy, "A", lit);
        if (b) text_centred(C, d, (SLOT(2) + SLOT(3)) / 2, cy, "B", lit);
        for (int k = 0; k < 4; k++) {
            if ((a && k < 2) || (b && k >= 2)) continue;
            if (s.ch == ORDER[k]) text_centred(C, d, SLOT(k), cy, DIGIT[k], lit);
            else dot(C, d, SLOT(k), cy, DOT_DIM_PX, dim);
        }
    }
#undef SLOT
}

void label_paint(const az_core_v1 *C, az_draw *d, az_rect r, fxd_state s, label_opts o)
{
    int named = s.valid || s.named; /* a value always comes with a known effect */
    int on = named && s.on;
    uint32_t bg = on ? GROUP_COL[fxd_group(s.effect)] : COL_OFF;
    uint32_t ink = on ? COL_TEXT_ON : COL_TEXT_OFF;
    C->fill_rect(d, r.x, r.y, r.w, r.h, bg);
    arrow(C, d, r, ink);
    if (!named) return;

    /* The value, in a light box over the stock tab's touch zone. v1.1: while the beat is not
     * confirmed (the TIME knob turned), TIME in ms in a grey box, as the stock panel greys its
     * beat box; "--" without TIME. */
    az_rect box = { TAB_X0 + ZONE_MARGIN, r.y + BOX_VPAD, BOX_W, r.h - 2 * BOX_VPAD };
    char value[8];
    int unsure = 0;
    if (on && s.xpad) snprintf(value, sizeof value, "X-PAD"); /* the pad's beat sounds, not BEAT's */
    else if (s.valid && s.beat) snprintf(value, sizeof value, "%s", s.beat);
    else if (s.valid) snprintf(value, sizeof value, "%d%%", s.percent);
    else {
        unsure = 1;
        if (s.ms > 0) snprintf(value, sizeof value, "%dms", s.ms > 65535 ? 65535 : s.ms);
        else snprintf(value, sizeof value, "--");
        if (s.ms > 0 && !fit_px(C, value, BEAT_PX_MAX, box.w - 4)) /* long TIMEs: digits alone */
            snprintf(value, sizeof value, "%d", s.ms > 65535 ? 65535 : s.ms);
    }
    C->fill_rect(d, box.x, box.y, box.w, box.h, unsure ? COL_BOX_UNSURE : COL_BOX);
    int vpx = fit_px(C, value, BEAT_PX_MAX, box.w - 4);
    if (vpx) {
        int vw = width(C, value, vpx);
        text_vcentred(C, d, box.x + (box.w - vw) / 2, box.y, box.h, value, vpx, COL_BOX_TEXT);
    }

    /* The name: full, smaller, then short, then cut to what fits. */
    int name_w = TAB_X0 - ZONE_MARGIN - (r.x + NAME_X);
    int name_max = NAME_PX_MAX;
    char name[24];
    snprintf(name, sizeof name, "%s", fxd_name(s.effect));
    int npx = fit_px(C, name, name_max, name_w);
    if (!npx) {
        snprintf(name, sizeof name, "%s", fxd_short_name(s.effect));
        npx = fit_px(C, name, name_max, name_w);
    }
    if (!npx) {
        npx = PX_MIN;
        size_t n = strlen(name);
        while (n > 0 && width(C, name, npx) > name_w) name[--n] = '\0';
    }
    if (!name[0]) return;
    int raise = o.row ? NAME_RAISE : 0;
    text_vcentred(C, d, r.x + NAME_X, r.y - raise, r.h, name, npx, ink);

    /* Off: underline the name in its group's colour. The capitals centre on the label's
     * middle and are about 0.72 px tall (measured on the deck), so they end 0.36 px below it. */
    if (!on)
        C->fill_rect(d, r.x + NAME_X, r.y - raise + r.h / 2 + npx * 37 / 100 + UNDERLINE_GAP,
                     width(C, name, npx), UNDERLINE_H, GROUP_COL[fxd_group(s.effect)]);

    if (o.row)
        channel_row(C, d, r, r.y + DOT_ROW_CY, s, ink,
                    on ? dimmed(bg) : COL_DOT_DIM);
}
