/* SPDX-License-Identifier: GPL-3.0-only
 *
 * label_draw: paints the label inside r with the core's draw helpers: background (grey,
 * or the effect group's colour while it sounds), the effect name (underlined in the group's
 * colour while off), the beat value (or LENGTH %; "X-PAD" while a finger on the pad makes
 * it sound) in a light box, and a left arrow at the
 * right end. The box and the arrow sit over the stock tab's touch zone (TAB_X0), the name
 * left of it. With s.valid == 0 and s.named == 1 (v1.1: the beat is not confirmed) the box is
 * grey and holds TIME in ms ("--" without TIME); with neither, only the grey tab and the arrow.
 *
 * v1.1: with o.row, a row under the name shows where the effect goes (CH SELECT): slots for
 * the channels 3 1 2 4, the selected one as its digit, the others as small dots; MST and MIC
 * as the word over all four, CF.A an A over the left pair, CF.B a B over the right pair.
 * Without it the name sits in the label's middle and there is no row.
 */
#ifndef BEATFX_LABEL_DRAW_H
#define BEATFX_LABEL_DRAW_H

#include "azmod-mod.h"
#include "fx_decode.h"

typedef struct {
    int row; /* 1 = the channel row under the name */
} label_opts;

void label_paint(const az_core_v1 *C, az_draw *d, az_rect r, fxd_state s, label_opts o);

#endif
