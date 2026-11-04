/* SPDX-License-Identifier: GPL-3.0-only
 *
 * panel_track: whether the deck's Beat FX panel is open, from where touches land. The core
 * does not expose the panel's state; a tap on the collapsed tab opens it and a tap on the
 * open panel's handle closes it. Each tap sets the state outright (never toggles), so a
 * wrong guess corrects itself at the next tap on either spot.
 */
#ifndef BEATFX_PANEL_TRACK_H
#define BEATFX_PANEL_TRACK_H

enum { PT_UNKNOWN = -1, PT_CLOSED = 0, PT_OPEN = 1 };

int pt_on_touch(int state, int x, int y);

#endif
