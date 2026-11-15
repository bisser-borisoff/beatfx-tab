/* SPDX-License-Identifier: GPL-3.0-only
 *
 * sign_draw: paints the Nudge Indicator box from a ready-made state, using only the SDK's draw
 * helpers. No state of its own, so it is safe to call from the paint thread.
 */
#ifndef PL_SIGN_DRAW_H
#define PL_SIGN_DRAW_H

#include "azmod-mod.h"

typedef struct {
    int locked;  /* 1: the green line only */
    int dx;      /* deck 2's tick, px right of deck 1's, -DX_MAX..DX_MAX */
    int ms;      /* the number, 0..999 */
} sign_state;

/* Paints the BOX_W x BOX_H box whose top left is area.x, area.y. Draw helpers only. */
void sign_paint(const az_core_v1 *C, az_draw *d, az_rect area, const sign_state *s);
int  sign_same(const sign_state *a, const sign_state *b);   /* 1: both paint the same picture */

#endif
