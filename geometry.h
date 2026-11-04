/* SPDX-License-Identifier: GPL-3.0-only
 *
 * geometry: every value to calibrate on the deck, in one place. Screen pixels (1280x800)
 * and 0xRRGGBB colours. First values: measured from photos and recorded touches
 * (2026-11-04); see the spec, sections 3 and 6.
 */
#ifndef BEATFX_GEOMETRY_H
#define BEATFX_GEOMETRY_H

#include "panel_track.h"

/* The mod's label, right-aligned over the stock tab and extended to the left. */
#define LABEL_X 1130
#define LABEL_Y 225
#define LABEL_W 150
#define LABEL_H 45

/* A touch here (to the right edge) is a tap on the stock tab: the panel opens. */
#define TAB_X0 1180
#define TAB_Y0 215
#define TAB_Y1 295

/* A touch here is a tap on the open panel's handle: the panel closes. */
#define HANDLE_X0 1035
#define HANDLE_X1 1100
#define HANDLE_Y0 215
#define HANDLE_Y1 320

/* Background: grey while the effect is off, the effect's X-PAD colour while it sounds. */
#define COL_OFF 0x8d939a
#define COL_BLUE 0x5ec4e8
#define COL_ORANGE 0xf0a42c
#define COL_YELLOW 0xd4e04a
#define COL_GREEN 0x86cc3a
#define COL_PURPLE 0xe070e8

#define COL_TEXT_OFF 0xffffff /* name and arrow on grey */
#define COL_TEXT_ON 0x111111  /* name and arrow on a colour */
#define COL_BOX 0xf2f2f2      /* the beat value's box */
#define COL_BOX_TEXT 0x222222

/* What the mod assumes about the panel when the deck starts (PT_UNKNOWN keeps the label
 * hidden until a tap on the tab or the handle tells). */
#define PANEL_BOOT_STATE PT_UNKNOWN

#endif
