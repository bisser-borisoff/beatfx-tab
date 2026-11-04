/* SPDX-License-Identifier: GPL-3.0-only
 *
 * label_draw: paints the label inside r with the core's draw helpers: background (grey,
 * or the effect group's colour while it sounds), a left arrow, the effect name, and the
 * beat value (or LENGTH %) in a light box on the right. With s.valid == 0 it paints only
 * the grey tab and the arrow.
 */
#ifndef BEATFX_LABEL_DRAW_H
#define BEATFX_LABEL_DRAW_H

#include "azmod-mod.h"
#include "fx_decode.h"

void label_paint(const az_core_v1 *C, az_draw *d, az_rect r, fxd_state s);

#endif
