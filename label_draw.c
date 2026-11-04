/* SPDX-License-Identifier: GPL-3.0-only */
#include "label_draw.h"

#include <stdio.h>
#include <string.h>

#include "geometry.h"

/* Layout inside the label, in pixels from its left / top edge. */
#define ARROW_X 6
#define ARROW_HALF 6 /* rows above and below the tip */
#define NAME_X 18
#define NAME_GAP 4 /* between the name and the box */
#define BOX_W 46
#define BOX_RIGHT 5
#define BOX_VPAD 7
#define NAME_PX_MAX 18
#define BEAT_PX_MAX 20
#define PX_MIN 12

static const uint32_t GROUP_COL[] = { COL_BLUE, COL_ORANGE, COL_YELLOW, COL_GREEN, COL_PURPLE };

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
    int x0 = r.x + ARROW_X, cy = r.y + r.h / 2;
    for (int k = -ARROW_HALF; k <= ARROW_HALF; k++) {
        int a = k < 0 ? -k : k;
        C->fill_rect(d, x0 + a, cy + k, ARROW_HALF + 1 - a, 1, rgb);
    }
}

static void text_vcentred(const az_core_v1 *C, az_draw *d, int x, int top, int h, const char *s,
                          int px, uint32_t rgb)
{
    C->draw_text(d, x, top + (h - px * 3 / 2) / 2, s, px, rgb);
}

void label_paint(const az_core_v1 *C, az_draw *d, az_rect r, fxd_state s)
{
    int on = s.valid && s.on;
    uint32_t bg = on ? GROUP_COL[fxd_group(s.effect)] : COL_OFF;
    uint32_t ink = on ? COL_TEXT_ON : COL_TEXT_OFF;
    C->fill_rect(d, r.x, r.y, r.w, r.h, bg);
    arrow(C, d, r, ink);
    if (!s.valid) return;

    /* The value, in a light box on the right. */
    az_rect box = { r.x + r.w - BOX_RIGHT - BOX_W, r.y + BOX_VPAD, BOX_W, r.h - 2 * BOX_VPAD };
    char value[8];
    if (s.beat) snprintf(value, sizeof value, "%s", s.beat);
    else snprintf(value, sizeof value, "%d%%", s.percent);
    C->fill_rect(d, box.x, box.y, box.w, box.h, COL_BOX);
    int vpx = fit_px(C, value, BEAT_PX_MAX, box.w - 4);
    if (vpx) {
        int vw = width(C, value, vpx);
        text_vcentred(C, d, box.x + (box.w - vw) / 2, box.y, box.h, value, vpx, COL_BOX_TEXT);
    }

    /* The name: full, smaller, then short, then cut to what fits. */
    int name_w = box.x - NAME_GAP - (r.x + NAME_X);
    char name[24];
    snprintf(name, sizeof name, "%s", fxd_name(s.effect));
    int npx = fit_px(C, name, NAME_PX_MAX, name_w);
    if (!npx) {
        snprintf(name, sizeof name, "%s", fxd_short_name(s.effect));
        npx = fit_px(C, name, NAME_PX_MAX, name_w);
    }
    if (!npx) {
        npx = PX_MIN;
        size_t n = strlen(name);
        while (n > 0 && width(C, name, npx) > name_w) name[--n] = '\0';
    }
    if (!name[0]) return;
    text_vcentred(C, d, r.x + NAME_X, r.y, r.h, name, npx, ink);

    /* Off: underline the name in its group's colour. The capitals centre on the label's
     * middle and are about 0.72 px tall (measured on the deck), so they end 0.36 px below it. */
    if (!on)
        C->fill_rect(d, r.x + NAME_X, r.y + r.h / 2 + npx * 37 / 100 + UNDERLINE_GAP,
                     width(C, name, npx), UNDERLINE_H, GROUP_COL[fxd_group(s.effect)]);
}
