/* SPDX-License-Identifier: GPL-3.0-only
 *
 * geometry: every value to calibrate on the deck, in one place. Screen pixels (1280x800)
 * and 0xRRGGBB colours. First values: measured from photos and recorded touches
 * (2026-11-04); see the spec, sections 3 and 6.
 */
#ifndef BEATFX_GEOMETRY_H
#define BEATFX_GEOMETRY_H

#include "panel_track.h"

/* The mod's label, right-aligned and opaque. It covers the stock tab, which photos on the
 * deck (2026-11-04, v0.1.0) put at about x 1197-1280, y 237-282, with a few pixels to spare. */
#define LABEL_X 1130
#define LABEL_Y 233
#define LABEL_W 150
#define LABEL_H 52

/* A touch here (to the right edge) opens the panel: on the deck a tap anywhere on the label
 * does. */
#define TAB_X0 LABEL_X
#define TAB_Y0 215
#define TAB_Y1 295

/* A touch here is a tap on the open panel's handle: the panel closes. Kept tight (about
 * 20 px around the taps recorded at 1067,266 and 1068,255): a wrong "closed" puts the label
 * over the open panel, a wrong "open" only shows the stock tab. */
#define HANDLE_X0 1047
#define HANDLE_X1 1088
#define HANDLE_Y0 235
#define HANDLE_Y1 290

/* Background: grey while the effect is off, the effect's X-PAD colour while it sounds.
 * v0.1.1: the grey darkened to the stock tab's look on the deck (white text was hard to read
 * on 0x8d939a), the blue darkened at Бисер's request (was 0x5ec4e8). */
#define COL_OFF 0x5d6268
#define COL_BLUE 0x3a96dc
#define COL_ORANGE 0xf0a42c
#define COL_YELLOW 0xd4e04a
#define COL_GREEN 0x86cc3a
#define COL_PURPLE 0xe070e8

#define COL_TEXT_OFF 0xffffff /* name and arrow on grey */
#define COL_TEXT_ON 0x111111  /* name and arrow on a colour */
#define COL_BOX 0xf2f2f2      /* the beat value's box */
#define COL_BOX_TEXT 0x222222

/* While the effect is off, a line in its group's colour under the name (v0.1.2, Бисер's
 * pick from the mockup: 3 px), UNDERLINE_GAP pixels below the letters. */
#define UNDERLINE_H 3
#define UNDERLINE_GAP 3

/* What the mod assumes about the panel when the deck starts (PT_UNKNOWN keeps the label
 * hidden until a tap on the tab or the handle tells). */
#define PANEL_BOOT_STATE PT_UNKNOWN

#endif
