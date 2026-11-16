/* SPDX-License-Identifier: GPL-3.0-only
 *
 * sign_draw: the painter of the Nudge Indicator box. Unlocked: deck 1's tick fixed, deck 2's tick
 * shifted by dx pixels (1 px per ms), the difference as a number with a grey "ms". Locked: one
 * green line as tall as the two ticks together. Tempo: the two-circle symbol where the ticks are and
 * the word TEMPO where the number is, both grey while the tempos differ, green when they match.
 */
#include <stdio.h>

#include "geometry.h"
#include "sign_draw.h"
#include "symbol.h"

#define NUM_BUF 16

void sign_paint(const az_core_v1 *C, az_draw *d, az_rect area, const sign_state *s)
{
    C->fill_rect(d, area.x, area.y, BOX_W, BOX_H, COL_BG);
    if (s->tempo) {
        if (AZ_HAS(C, draw_image))
            C->draw_image(d, area.x + SYM_DX, area.y + SYM_DY, s->match ? SYM_MATCH : SYM_DIFF, SYM_W, SYM_H);
        C->draw_text(d, area.x + WORD_DX, area.y + WORD_DY, WORD, WORD_PX, s->match ? COL_LOCK : COL_UNIT);
        return;
    }
    const int tx = area.x + TICK_DX;
    if (s->locked) {
        C->fill_rect(d, tx, area.y + TICK1_DY, TICK_W, TICK2_DY + TICK_H - TICK1_DY, COL_LOCK);
        return;
    }
    C->fill_rect(d, tx, area.y + TICK1_DY, TICK_W, TICK_H, COL_TICK);
    C->fill_rect(d, tx + s->dx, area.y + TICK2_DY, TICK_W, TICK_H, COL_TICK);

    char num[NUM_BUF];
    snprintf(num, sizeof num, "%d", s->ms);
    const int nx = area.x + NUM_DX;
    C->draw_text(d, nx, area.y + NUM_DY, num, NUM_PX, COL_NUM);
    C->draw_text(d, nx + C->text_width(num, NUM_PX) + UNIT_GAP, area.y + UNIT_DY, "ms", UNIT_PX, COL_UNIT);
}

int sign_same(const sign_state *a, const sign_state *b)
{
    if (a->tempo || b->tempo) return a->tempo == b->tempo && a->match == b->match;
    if (a->locked || b->locked) return a->locked == b->locked;
    return a->dx == b->dx && a->ms == b->ms;
}
