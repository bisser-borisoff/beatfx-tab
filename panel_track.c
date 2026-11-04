/* SPDX-License-Identifier: GPL-3.0-only */
#include "panel_track.h"

#include "geometry.h"

int pt_on_touch(int state, int x, int y)
{
    if (x >= TAB_X0 && y >= TAB_Y0 && y <= TAB_Y1) return PT_OPEN;
    if (x >= HANDLE_X0 && x <= HANDLE_X1 && y >= HANDLE_Y0 && y <= HANDLE_Y1) return PT_CLOSED;
    return state;
}
