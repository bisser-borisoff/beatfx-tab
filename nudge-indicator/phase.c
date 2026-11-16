/* SPDX-License-Identifier: GPL-3.0-only */
#include <math.h>

#include "phase.h"
#include "tunables.h"

#define MS_PER_MIN 60000.0
#define HALF       0.5

double pl_delta_ms(double ph1, double ph2, double bpm1, double bpm2)
{
    double d = ph1 - ph2;
    d -= floor(d + HALF);                       /* into [-0.5, 0.5) */
    double mean = (bpm1 + bpm2) * HALF;
    return d * MS_PER_MIN / mean;
}

int pl_dx(double delta_ms)
{
    double r = floor(delta_ms + HALF);
    if (r > DX_MAX) r = DX_MAX;
    if (r < -DX_MAX) r = -DX_MAX;
    return (int)r;
}

int pl_show_ms(double delta_ms)
{
    return (int)floor(fabs(delta_ms) + HALF);
}

static int ph_ok(double ph) { return isfinite(ph) && ph >= 0.0 && ph <= 1.0; }
static int bpm_ok(double bpm) { return isfinite(bpm) && bpm > 0.0; }

/* The first failing check before the tempo comparison, or PL_OK. */
static pl_why gate(const pl_in *in)
{
    if (!in->row_on) return PL_ROW_OFF;
    if (in->theme) return PL_THEME;
    if (in->deck_count != 2) return PL_NOT_2DECK;
    if (in->deck34) return PL_DECK34;
    if (!in->loaded[0]) return PL_NO_TRACK_1;
    if (!in->loaded[1]) return PL_NO_TRACK_2;
    if (!in->playing[0]) return PL_STOPPED_1;
    if (!in->playing[1]) return PL_STOPPED_2;
    if (!in->grid_ok[0] || !ph_ok(in->ph[0])) return PL_NO_GRID_1;
    if (!in->grid_ok[1] || !ph_ok(in->ph[1])) return PL_NO_GRID_2;
    if (!bpm_ok(in->bpm[0])) return PL_NO_TEMPO_1;
    if (!bpm_ok(in->bpm[1])) return PL_NO_TEMPO_2;
    return PL_OK;
}

/* The screen is ours, decks 1 and 2 are loaded and both tempos are known: the tempo symbol can show. */
static int tempo_ok(const pl_in *in)
{
    return in->row_on && !in->theme && in->deck_count == 2 && !in->deck34 && in->loaded[0] && in->loaded[1] &&
           bpm_ok(in->bpm[0]) && bpm_ok(in->bpm[1]);
}

/* The symbol's answer: the first one counts at once, a change only after it held TEMPO_SYM_HOLD_MS. */
static int tempo_match(pl_sm *sm, const pl_in *in)
{
    const int raw = fabs(in->bpm[0] - in->bpm[1]) <= TEMPO_MATCH_BPM;
    if (!sm->sym_seen) {
        sm->sym_seen = 1;
        sm->sym_match = raw;
        sm->sym_pend = 0;
    } else if (raw == sm->sym_match) {
        sm->sym_pend = 0;
    } else if (!sm->sym_pend) {
        sm->sym_pend = 1;
        sm->sym_since = in->t_ms;
    } else if ((uint32_t)(in->t_ms - sm->sym_since) >= TEMPO_SYM_HOLD_MS) {
        sm->sym_match = raw;
        sm->sym_pend = 0;
    }
    return sm->sym_match;
}

void pl_step(pl_sm *sm, const pl_in *in, pl_out *out)
{
    out->state = PL_HIDDEN;
    out->why = PL_OK;
    out->have_delta = 0;
    out->delta_ms = 0.0;
    out->dx = 0;
    out->ms = 0;
    out->have_tempo = 0;
    out->tempo_match = 0;

    /* a late tick starts fresh: nothing remembered, the previous state counts as hidden */
    if (sm->seen && (uint32_t)(in->t_ms - sm->last_t) > TICK_GAP_MS) {
        sm->near = 0;
        sm->tempo_bad = 0;
        sm->far = 0;
        sm->sym_seen = 0;
        sm->state = PL_HIDDEN;
    }
    sm->seen = 1;
    sm->last_t = in->t_ms;
    const pl_state prev = sm->state;
    const int prev_reading = prev == PL_NUDGE || prev == PL_LOCKED;

    pl_why why = gate(in);
    if (why == PL_OK) {
        double d = pl_delta_ms(in->ph[0], in->ph[1], in->bpm[0], in->bpm[1]);
        out->have_delta = 1;
        out->delta_ms = d;
        out->dx = pl_dx(d);
        out->ms = pl_show_ms(d);

        double mean = (in->bpm[0] + in->bpm[1]) * HALF;
        int tempo_diff = 0;
        if (fabs(in->bpm[0] - in->bpm[1]) / mean <= TEMPO_TOL) {
            sm->tempo_bad = 0;
        } else if (!prev_reading) {
            tempo_diff = 1;
        } else {
            if (!sm->tempo_bad) {
                sm->tempo_bad = 1;
                sm->tempo_bad_since = in->t_ms;
            }
            tempo_diff = (uint32_t)(in->t_ms - sm->tempo_bad_since) >= TEMPO_HOLD_MS;
        }

        if (tempo_diff) {
            why = PL_TEMPO_DIFF;
        } else {
            double ad = fabs(d);
            if (ad <= LOCK_IN_MS) {
                if (!sm->near) {
                    sm->near = 1;
                    sm->near_since = in->t_ms;
                }
            } else {
                sm->near = 0;
            }
            /* locked: the line stays up to LOCK_OUT_MS, through a short excursion over it, and goes at
             * once only over LOCK_HARD_OUT_MS */
            int keep = 0;
            if (prev == PL_LOCKED && ad <= LOCK_HARD_OUT_MS) {
                if (ad <= LOCK_OUT_MS) {
                    sm->far = 0;
                    keep = 1;
                } else {
                    if (!sm->far) {
                        sm->far = 1;
                        sm->far_since = in->t_ms;
                    }
                    keep = (uint32_t)(in->t_ms - sm->far_since) < LOCK_OUT_HOLD_MS;
                }
            }
            if (keep) {
                out->state = PL_LOCKED;
            } else if (sm->near && (uint32_t)(in->t_ms - sm->near_since) >= LOCK_HOLD_MS) {
                out->state = PL_LOCKED;
            } else {
                out->state = PL_NUDGE;
                why = ad > LOCK_IN_MS ? PL_APART : PL_HOLDING;
            }
            if (out->state != PL_LOCKED) sm->far = 0;
        }
    }
    out->why = why;
    if (out->state == PL_HIDDEN) { /* no reading: its timers start anew */
        sm->near = 0;
        sm->tempo_bad = 0;
        sm->far = 0;
    }
    if (tempo_ok(in)) {
        out->have_tempo = 1;
        out->tempo_match = tempo_match(sm, in);
        if (out->state == PL_HIDDEN) out->state = PL_TEMPO; /* the symbol stands where the reading would */
    } else {
        sm->sym_seen = 0;
    }
    sm->state = out->state;
}
