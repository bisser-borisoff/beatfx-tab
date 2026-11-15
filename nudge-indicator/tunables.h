/* SPDX-License-Identifier: GPL-3.0-only */
/* nudge-indicator: all tunables in one place. */
#ifndef PL_TUNABLES_H
#define PL_TUNABLES_H

#define LOCK_IN_MS        5.0  /* |delta| at or under this, held LOCK_HOLD_MS, locks */
#define LOCK_HOLD_MS      250
#define LOCK_OUT_MS       5.0  /* locked: |delta| over this for LOCK_OUT_HOLD_MS without a break unlocks */
#define LOCK_OUT_HOLD_MS  300  /* a shorter excursion is ignored (the deck's readings jump for a moment) */
#define LOCK_HARD_OUT_MS  10.0 /* locked: |delta| over this unlocks at once */
#define TEMPO_TOL       0.002  /* |bpm1 - bpm2| / mean */
#define TEMPO_HOLD_MS   2000   /* a shown sign survives a tempo mismatch this long (jog nudges) */
#define TICK_GAP_MS     500    /* a tick later than this after the previous one starts fresh */
#define DX_MAX          14     /* px; 1 px per ms */
#define NUDGE_REDRAW_MS 125    /* at most 8 repaints a second while nudging */
#define PAGE_REFRESH_MS 250
#define PAGE_ALIVE_MS   700
#define IN_PHASE_MS     0.5    /* the Status page says "in phase" under this |delta| */
#define MS_MAX          999    /* the number the sign can show */

#endif
