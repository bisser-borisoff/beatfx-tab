/* SPDX-License-Identifier: GPL-3.0-only */
/* phase: the beat-phase difference of decks 1 and 2, the gate and the lock state machine.
 * Pure: no SDK calls, no clock reads (time comes in through pl_in.t_ms). */
#ifndef PL_PHASE_H
#define PL_PHASE_H

#include <stdint.h>

/* Deck 1 minus deck 2, in ms. Positive: deck 1 is ahead. Phases are 0..1; bpm > 0. */
double pl_delta_ms(double ph1, double ph2, double bpm1, double bpm2);
int    pl_dx(double delta_ms);       /* round to nearest, clamped to [-DX_MAX, DX_MAX] */
int    pl_show_ms(double delta_ms);  /* |delta| rounded to nearest whole ms */

typedef enum { PL_HIDDEN = 0, PL_NUDGE, PL_LOCKED } pl_state;

/* Why the sign is not "locked". The first failing check, in this order (design 5.2, 5.3, 6.3).
 * The view is not one of them: the mod shell hides the surface when it is not the deck view. */
typedef enum {
    PL_OK = 0,        /* locked */
    PL_ROW_OFF,       /* our toggle is off */
    PL_THEME,         /* a theme is on */
    PL_NOT_2DECK,     /* deck_count != 2 */
    PL_DECK34,        /* deck 3 or 4 has a track */
    PL_NO_TRACK_1, PL_NO_TRACK_2,
    PL_STOPPED_1, PL_STOPPED_2,
    PL_NO_GRID_1, PL_NO_GRID_2,
    PL_NO_TEMPO_1, PL_NO_TEMPO_2,
    PL_TEMPO_DIFF,    /* tempos differ by more than TEMPO_TOL (after the hold) */
    PL_APART,         /* |delta| over the threshold: nudging */
    PL_HOLDING        /* within LOCK_IN_MS but not yet for LOCK_HOLD_MS: nudging */
} pl_why;

typedef struct {
    uint32_t t_ms;                 /* the UI clock, wraps */
    int row_on, theme, deck_count, deck34;
    int loaded[2], playing[2], grid_ok[2];
    double bpm[2], ph[2];
} pl_in;

typedef struct {                   /* zero-initialised = hidden, nothing remembered */
    pl_state state;
    int      near;      uint32_t near_since;       /* |delta| <= LOCK_IN_MS since */
    int      tempo_bad; uint32_t tempo_bad_since;  /* tempo out of TEMPO_TOL since */
    int      far;       uint32_t far_since;        /* locked, and |delta| > LOCK_OUT_MS since */
    int      seen;      uint32_t last_t;           /* the previous tick's clock */
} pl_sm;

typedef struct {
    pl_state state;
    pl_why   why;        /* the first failing check */
    int      have_delta; /* delta_ms, dx and ms are valid (checks up to PL_NO_TEMPO_2 passed) */
    double   delta_ms;
    int      dx, ms;
} pl_out;

void pl_step(pl_sm *sm, const pl_in *in, pl_out *out);

#endif
