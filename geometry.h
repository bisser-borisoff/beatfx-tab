/* SPDX-License-Identifier: GPL-3.0-only
 *
 * geometry: every value to calibrate on the deck, in one place. Screen pixels (1280x800)
 * and 0xRRGGBB colours. First values: measured from photos and recorded touches
 * (2026-11-04), then calibrated on the deck in v0.1.1 to v0.1.4.
 */
#ifndef BEATFX_GEOMETRY_H
#define BEATFX_GEOMETRY_H

#include "panel_track.h"

/* The mod's label, right-aligned and opaque. It covers the stock tab, which photos on the
 * deck (2026-11-04, v0.1.0) put at about x 1197-1280, y 237-282, with a few pixels to spare.
 * v0.1.3: 24 px wider to the left, so the name has its room left of the tab's touch zone. */
#define LABEL_X 1106
#define LABEL_Y 233
#define LABEL_W 174
#define LABEL_H 52

/* Only the stock tab's own zone opens the panel, from TAB_X0 (its left edge on the photos) to
 * the right edge: on the deck a tap on the name, left of it, did not, and a tap in the grey
 * just left of the value box did (b.sr, 2026-11-04). The arrow and the value box are drawn
 * inside it and the name left of it, ZONE_MARGIN pixels clear of the edge either side. */
#define TAB_X0 1197
#define TAB_Y0 215
#define TAB_Y1 295
#define ZONE_MARGIN 6

/* A touch here is a tap on the open panel's handle: the panel closes. Kept tight (about
 * 20 px around the taps recorded at 1067,266 and 1068,255): a wrong "closed" puts the label
 * over the open panel, a wrong "open" only shows the stock tab. */
#define HANDLE_X0 1047
#define HANDLE_X1 1088
#define HANDLE_Y0 235
#define HANDLE_Y1 290

/* Background: grey while the effect is off, the effect's X-PAD colour while it sounds.
 * v0.1.1: the grey darkened to the stock tab's look on the deck (white text was hard to read
 * on 0x8d939a). v0.1.7: the group colours matched by eye to the stock panel's X-PAD bar, on
 * the deck, against swatches beside the open panel (b.sr, two rounds); the purple is the
 * closest swatch made a little brighter. v1.1 (b.sr, 2026-11-06), from a screenshot of the
 * deck (xwd over SSH) instead of photos: the grey is the deck UI's card colour (the track title
 * strip, the deck panels, X-PAD PARAMETER), the blue the stock X-PAD bar's exact blue (the
 * earlier tries 0x00b4f0 and 0x0096d2 were matched by eye; 0x28a0ff was the Phase Meter's). */
#define COL_OFF 0x232323
#define COL_BLUE 0x009be1
#define COL_ORANGE 0xf07828
#define COL_LIME 0x98d02c /* FLANGER, PHASER, FILTER, TRIPLET FILTER, TRANS */
#define COL_GREEN 0x3c9820 /* ROLL, TRIPLET ROLL */
#define COL_PURPLE 0xc050ff

#define COL_TEXT_OFF 0xffffff /* name and arrow on grey */
#define COL_TEXT_ON 0x111111  /* name and arrow on a colour */
#define COL_BOX 0xf2f2f2      /* the beat value's box */
#define COL_BOX_TEXT 0x222222
#define COL_BOX_UNSURE 0xa0a0a0 /* v1.1: the box while the beat is not confirmed (TIME in ms),
                                 * as the stock panel greys its beat box when TIME is off a beat */

/* While the effect is off, a line in its group's colour under the name (v0.1.2, b.sr's
 * pick from the mockup: 3 px), UNDERLINE_GAP pixels below the letters. */
#define UNDERLINE_H 3
#define UNDERLINE_GAP 3

/* v1.1: the channel row under the name (b.sr's picks from the mockups and the deck,
 * 2026-11-06): four slots in the order of the CH SELECT buttons (3 1 2 4), the first one on
 * the name's left edge. DOT_SLOT0_CX is the first slot's centre, DOT_PITCH the step. The
 * selected channel shows its digit, the others a DOT_DIM_PX dot in COL_DOT_DIM (effect off)
 * or the group colour at 70% (effect on). MST and MIC show the word over all four, CF.A an A
 * over the left pair, CF.B a B over the right pair. With the row on, the name moves up. */
#define NAME_RAISE 9
#define DOT_ROW_CY 38 /* the row's centre, from the label's top; with NAME_RAISE, the name and
                       * the row sit in the label's middle (9 px of ground above and below,
                       * measured on a screenshot of the deck, 2026-11-06; was 4 and 43) */
#define NAME_X 10 /* the name's inset, as the arrow's room on the right (b.sr, 1.0.98; was 6) */
#define DOT_SLOT0_CX (NAME_X + 4)
#define DOT_PITCH 13
#define DOT_DIM_PX 4
#define DOT_TEXT_PX 12 /* the digits, MST, MIC, A and B */
#define COL_DOT_DIM 0x868b91

/* What the mod assumes about the panel when the deck starts (PT_UNKNOWN keeps the label
 * hidden until a tap on the tab or the handle tells). */
#define PANEL_BOOT_STATE PT_UNKNOWN

#endif
