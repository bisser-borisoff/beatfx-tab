/* SPDX-License-Identifier: GPL-3.0-only */
#include <math.h>
#include <stdio.h>

#include "phase.h"
#include "tunables.h"

static int total, passed;
#define CHECK(c) do { total++; if (c) passed++; else printf("FAIL: %s (line %d)\n", #c, __LINE__); } while (0)

static void test_delta(void)
{
    CHECK(fabs(pl_delta_ms(0.50, 0.40, 120.0, 120.0) - 50.0) < 1e-9);   /* 0.1 beat of 500 ms */
    CHECK(fabs(pl_delta_ms(0.40, 0.50, 120.0, 120.0) + 50.0) < 1e-9);
    CHECK(fabs(pl_delta_ms(0.95, 0.05, 120.0, 120.0) + 50.0) < 1e-9);   /* wraps: -0.1 beat */
    CHECK(fabs(pl_delta_ms(0.05, 0.95, 120.0, 120.0) - 50.0) < 1e-9);
    CHECK(fabs(pl_delta_ms(0.75, 0.25, 120.0, 120.0) + 250.0) < 1e-9);  /* exactly half a beat reads -0.5 */
    CHECK(fabs(pl_delta_ms(0.30, 0.30, 125.0, 124.9)) < 1e-9);
    CHECK(fabs(pl_delta_ms(0.50, 0.40, 125.0, 123.0) - 0.1 * 60000.0 / 124.0) < 1e-9); /* mean tempo */
}

static void test_dx_and_number(void)
{
    CHECK(pl_dx(0.4) == 0 && pl_dx(0.6) == 1 && pl_dx(-0.6) == -1);
    CHECK(pl_dx(12.2) == 12 && pl_dx(14.0) == 14 && pl_dx(14.6) == 14 && pl_dx(120.0) == 14);
    CHECK(pl_dx(-39.0) == -14);
    CHECK(pl_show_ms(0.4) == 0 && pl_show_ms(-3.6) == 4 && pl_show_ms(119.5) == 120);
}

/* ---- Task 2: the gate and the lock state machine ---- */

#define TICK_MS 40
#define MS_PER_BEAT_125 480.0   /* 125 BPM: 1 ms = 1/480 beat */

static pl_in base(uint32_t t)
{
    pl_in in = {0};
    in.t_ms = t;
    in.row_on = 1; in.theme = 0; in.deck_count = 2; in.deck34 = 0;
    in.loaded[0] = in.loaded[1] = 1;
    in.playing[0] = in.playing[1] = 1;
    in.grid_ok[0] = in.grid_ok[1] = 1;
    in.bpm[0] = in.bpm[1] = 125.0;
    in.ph[0] = in.ph[1] = 0.30;
    return in;
}
/* deck 1 ahead of deck 2 by ms (at 125 BPM) */
static void set_delta(pl_in *in, double ms)
{
    in->ph[0] = 0.30 + ms / MS_PER_BEAT_125;
    in->ph[1] = 0.30;
}
static pl_out run(pl_sm *sm, pl_in in)
{
    pl_out out;
    pl_step(sm, &in, &out);
    return out;
}

static void brk_row(pl_in *in)    { in->row_on = 0; }
static void brk_theme(pl_in *in)  { in->theme = 1; }
static void brk_count(pl_in *in)  { in->deck_count = 4; }
static void brk_34(pl_in *in)     { in->deck34 = 1; }
static void brk_load1(pl_in *in)  { in->loaded[0] = 0; }
static void brk_load2(pl_in *in)  { in->loaded[1] = 0; }
static void brk_play1(pl_in *in)  { in->playing[0] = 0; }
static void brk_play2(pl_in *in)  { in->playing[1] = 0; }
static void brk_grid1(pl_in *in)  { in->grid_ok[0] = 0; }
static void brk_grid2(pl_in *in)  { in->grid_ok[1] = 0; }
static void brk_bpm1(pl_in *in)   { in->bpm[0] = 0.0; }
static void brk_bpm2(pl_in *in)   { in->bpm[1] = 0.0; }
static void brk_tempo(pl_in *in)   /* 0.24 %; leaves a reading an earlier break already spoiled */
{
    if (in->bpm[0] > 0.0 && in->bpm[1] > 0.0) { in->bpm[0] = 125.0; in->bpm[1] = 125.3; }
}

static void test_gate_order(void)   /* one line per reason, each from a fresh pl_sm */
{
    struct { pl_why why; void (*brk)(pl_in *); } T[] = {
        { PL_ROW_OFF, brk_row }, { PL_THEME, brk_theme }, { PL_NOT_2DECK, brk_count }, { PL_DECK34, brk_34 },
        { PL_NO_TRACK_1, brk_load1 }, { PL_NO_TRACK_2, brk_load2 },
        { PL_STOPPED_1, brk_play1 }, { PL_STOPPED_2, brk_play2 }, { PL_NO_GRID_1, brk_grid1 },
        { PL_NO_GRID_2, brk_grid2 }, { PL_NO_TEMPO_1, brk_bpm1 }, { PL_NO_TEMPO_2, brk_bpm2 },
        { PL_TEMPO_DIFF, brk_tempo },
    };
    const int n = (int)(sizeof T / sizeof T[0]);
    for (int i = 0; i < n; i++) {
        pl_sm sm = {0};
        pl_in in = base(1000);
        T[i].brk(&in);
        pl_out o = run(&sm, in);
        CHECK(o.state == PL_HIDDEN && o.why == T[i].why);
    }
    /* two breaks at once: the earlier one in the table wins */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            pl_sm sm = {0};
            pl_in in = base(1000);
            T[i].brk(&in);
            T[j].brk(&in);
            pl_out o = run(&sm, in);
            CHECK(o.state == PL_HIDDEN && o.why == T[i].why);
        }
    }
    /* and with everything fine the fresh machine is in the "settling" state, not hidden */
    {
        pl_sm sm = {0};
        pl_out o = run(&sm, base(1000));
        CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING && o.have_delta == 1);
    }
}

/* n ticks of 40 ms at delta ms, starting at *t; returns the last result */
static pl_out feed(pl_sm *sm, uint32_t *t, double ms, int n)
{
    pl_out o = {0};
    for (int k = 0; k < n; k++, *t += TICK_MS) {
        pl_in in = base(*t);
        set_delta(&in, ms);
        o = run(sm, in);
    }
    return o;
}
/* every one of n ticks at delta ms gives this state and reason */
static void expect(pl_sm *sm, uint32_t *t, double ms, int n, pl_state st, pl_why why)
{
    for (int k = 0; k < n; k++, *t += TICK_MS) {
        pl_in in = base(*t);
        set_delta(&in, ms);
        pl_out o = run(sm, in);
        CHECK(o.state == st && o.why == why);
    }
}

static void test_lock_hold_and_hysteresis(void)
{   /* 125 BPM: 1 ms = 1/480 beat. Ticks every 40 ms. */
    pl_sm sm = {0};
    uint32_t t = 1000;
    pl_out o;

    /* locking: within LOCK_IN_MS (5 ms) for LOCK_HOLD_MS (250 ms) */
    expect(&sm, &t, 2.0, 7, PL_NUDGE, PL_HOLDING);      /* t = 1000 .. 1240: held 240 ms */
    CHECK(t == 1280);
    expect(&sm, &t, 2.0, 1, PL_LOCKED, PL_OK);           /* 280 ms held */

    /* up to LOCK_OUT_MS (5 ms) the line stays, for as long as it likes */
    expect(&sm, &t, 4.5, 20, PL_LOCKED, PL_OK);
    expect(&sm, &t, -4.9, 20, PL_LOCKED, PL_OK);

    /* a short excursion over 5 ms does not unlock: 8 ms for two ticks, then back */
    expect(&sm, &t, 8.0, 2, PL_LOCKED, PL_OK);
    expect(&sm, &t, 2.0, 3, PL_LOCKED, PL_OK);

    /* over 5 ms without a break: still locked through 280 ms, unlocked at 320 ms (LOCK_OUT_HOLD_MS 300) */
    expect(&sm, &t, 5.5, 8, PL_LOCKED, PL_OK);           /* +0 .. +280 */
    expect(&sm, &t, 5.5, 1, PL_NUDGE, PL_APART);         /* +320 */

    /* back within 5 ms: nudging until 250 ms have passed again */
    expect(&sm, &t, 4.0, 7, PL_NUDGE, PL_HOLDING);
    expect(&sm, &t, 4.0, 1, PL_LOCKED, PL_OK);

    /* the excursion timer starts anew each time: 5.5 ms for 280 ms, a tick at 2 ms, 5.5 ms for 280 ms */
    expect(&sm, &t, 5.5, 8, PL_LOCKED, PL_OK);
    expect(&sm, &t, 2.0, 1, PL_LOCKED, PL_OK);
    expect(&sm, &t, 5.5, 8, PL_LOCKED, PL_OK);
    expect(&sm, &t, 2.0, 1, PL_LOCKED, PL_OK);

    /* just under LOCK_HARD_OUT_MS (10 ms) is still an excursion; over it unlocks at once
     * (9.9 and 4.9, not 10.0 and 5.0: the phases are doubles and the round trip is not exact) */
    expect(&sm, &t, 9.9, 1, PL_LOCKED, PL_OK);
    expect(&sm, &t, 2.0, 1, PL_LOCKED, PL_OK);
    expect(&sm, &t, 10.5, 1, PL_NUDGE, PL_APART);
    expect(&sm, &t, -10.5, 1, PL_NUDGE, PL_APART);

    /* 4.5 ms from the start locks too: the threshold to lock is 5 ms */
    {
        pl_sm sm2 = {0};
        uint32_t t2 = 5000;
        expect(&sm2, &t2, 4.5, 7, PL_NUDGE, PL_HOLDING);
        expect(&sm2, &t2, 4.5, 1, PL_LOCKED, PL_OK);
    }
    /* a 6 ms tick in the middle of a hold restarts it: 2,2,2,6,2,... */
    {
        pl_sm sm3 = {0};
        uint32_t t3 = 9000;
        expect(&sm3, &t3, 2.0, 3, PL_NUDGE, PL_HOLDING);
        expect(&sm3, &t3, 6.0, 1, PL_NUDGE, PL_APART);
        expect(&sm3, &t3, 2.0, 7, PL_NUDGE, PL_HOLDING);   /* 0 .. 240 ms after the restart */
        expect(&sm3, &t3, 2.0, 1, PL_LOCKED, PL_OK);
    }
    /* an excursion does not survive a hidden tick: locked, 5.5 ms for 200 ms, deck 2 stops for a tick,
     * then 2 ms: nudging for 250 ms again (nothing is remembered) */
    {
        pl_sm sm4 = {0};
        uint32_t t4 = 20000;
        o = feed(&sm4, &t4, 2.0, 8);
        CHECK(o.state == PL_LOCKED);
        expect(&sm4, &t4, 5.5, 5, PL_LOCKED, PL_OK);
        { pl_in in = base(t4); in.playing[1] = 0; o = run(&sm4, in); t4 += TICK_MS; }
        CHECK(o.state == PL_HIDDEN && o.why == PL_STOPPED_2);
        expect(&sm4, &t4, 2.0, 7, PL_NUDGE, PL_HOLDING);
        expect(&sm4, &t4, 2.0, 1, PL_LOCKED, PL_OK);
    }
}

static void test_tempo_hold(void)
{
    const uint32_t t0 = 10000;
    pl_sm sm = {0};
    pl_out o;
    /* shown (NUDGE, delta 10 ms) */
    { pl_in in = base(t0 - TICK_MS); set_delta(&in, 10.0); o = run(&sm, in); }
    CHECK(o.state == PL_NUDGE && o.why == PL_APART);
    /* bpm {125.0, 126.0} from t0: still NUDGE at t0+1960; HIDDEN, PL_TEMPO_DIFF at t0+2000 */
    for (uint32_t t = t0; t <= t0 + 1960; t += TICK_MS) {
        pl_in in = base(t); set_delta(&in, 10.0); in.bpm[1] = 126.0;
        o = run(&sm, in);
        CHECK(o.state == PL_NUDGE && o.why == PL_APART);
    }
    { pl_in in = base(t0 + 2000); set_delta(&in, 10.0); in.bpm[1] = 126.0; o = run(&sm, in); }
    CHECK(o.state == PL_HIDDEN && o.why == PL_TEMPO_DIFF);

    /* tempo back in range at t0+1000 clears the timer: a second mismatch needs 2000 ms again */
    {
        pl_sm sm2 = {0};
        { pl_in in = base(t0 - TICK_MS); set_delta(&in, 10.0); o = run(&sm2, in); }
        for (uint32_t t = t0; t < t0 + 1000; t += TICK_MS) {
            pl_in in = base(t); set_delta(&in, 10.0); in.bpm[1] = 126.0;
            o = run(&sm2, in);
            CHECK(o.state == PL_NUDGE);
        }
        { pl_in in = base(t0 + 1000); set_delta(&in, 10.0); o = run(&sm2, in); }   /* in range */
        CHECK(o.state == PL_NUDGE && o.why == PL_APART);
        const uint32_t t1 = t0 + 1040;   /* the second mismatch starts */
        for (uint32_t t = t1; t <= t1 + 1960; t += TICK_MS) {
            pl_in in = base(t); set_delta(&in, 10.0); in.bpm[1] = 126.0;
            o = run(&sm2, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_APART);
        }
        { pl_in in = base(t1 + 2000); set_delta(&in, 10.0); in.bpm[1] = 126.0; o = run(&sm2, in); }
        CHECK(o.state == PL_HIDDEN && o.why == PL_TEMPO_DIFF);
    }

    /* from HIDDEN, the same mismatch hides at once (never shows) */
    {
        pl_sm sm3 = {0};
        for (uint32_t t = t0; t < t0 + 3000; t += TICK_MS) {
            pl_in in = base(t); set_delta(&in, 10.0); in.bpm[1] = 126.0;
            o = run(&sm3, in);
            CHECK(o.state == PL_HIDDEN && o.why == PL_TEMPO_DIFF);
        }
    }
}

static void test_stop_while_locked(void)
{   /* locked; deck 2's phase freezes while deck 1 advances 40 ms a tick, playing still {1,1} */
    pl_sm sm = {0};
    uint32_t t = 1000;
    pl_out o = {0};
    while (t <= 1000 + LOCK_HOLD_MS + TICK_MS) {
        o = run(&sm, base(t));
        t += TICK_MS;
    }
    CHECK(o.state == PL_LOCKED);
    /* first tick after the stop: delta 40 ms -> NUDGE (playing[1] still 1) */
    { pl_in in = base(t); set_delta(&in, 40.0); o = run(&sm, in); t += TICK_MS; }
    CHECK(o.state == PL_NUDGE && o.why == PL_APART);
    CHECK(o.dx == DX_MAX && o.ms == 40);
    /* then playing[1] = 0 -> HIDDEN */
    { pl_in in = base(t); set_delta(&in, 80.0); in.playing[1] = 0; o = run(&sm, in); }
    CHECK(o.state == PL_HIDDEN && o.why == PL_STOPPED_2);
}

static void test_bad_readings(void)
{
    const double bad_bpm[] = { 0.0, -1.0, NAN };
    const double bad_ph[]  = { NAN, -0.1, 1.5 };
    for (int deck = 0; deck < 2; deck++) {
        for (int i = 0; i < 3; i++) {
            pl_sm sm = {0};
            pl_in in = base(1000);
            in.bpm[deck] = bad_bpm[i];
            pl_out o = run(&sm, in);
            CHECK(o.state == PL_HIDDEN && o.why == (deck == 0 ? PL_NO_TEMPO_1 : PL_NO_TEMPO_2));
            CHECK(o.have_delta == 0 && isnan(o.delta_ms) == 0);

            pl_sm sm2 = {0};
            pl_in in2 = base(1000);
            in2.ph[deck] = bad_ph[i];
            o = run(&sm2, in2);
            CHECK(o.state == PL_HIDDEN && o.why == (deck == 0 ? PL_NO_GRID_1 : PL_NO_GRID_2));
            CHECK(o.have_delta == 0 && isnan(o.delta_ms) == 0);
        }
    }
    /* a bad reading while shown hides at once and the paint numbers stay finite */
    {
        pl_sm sm = {0};
        pl_in in = base(1000); set_delta(&in, 10.0);
        pl_out o = run(&sm, in);
        CHECK(o.state == PL_NUDGE);
        in = base(1040); in.bpm[0] = NAN;
        o = run(&sm, in);
        CHECK(o.state == PL_HIDDEN && o.why == PL_NO_TEMPO_1 && o.have_delta == 0);
        CHECK(isnan(o.delta_ms) == 0 && o.dx == 0 && o.ms == 0);
    }
}

static void test_tick_gap_and_wrap(void)
{
    pl_out o;
    const uint32_t START = 1000;
    /* (a) near for 120 ms in 40 ms ticks, then the next tick comes 30000 ms later, still near */
    {
        pl_sm sm = {0};
        uint32_t t = START;
        for (int k = 0; k < 4; k++, t += TICK_MS) {   /* 1000..1120 */
            pl_in in = base(t); set_delta(&in, 2.0);
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
        }
        t = 1120 + 30000;
        const uint32_t after_gap = t;
        for (int k = 0; k < 7; k++, t += TICK_MS) {   /* after_gap .. +240: no lock */
            pl_in in = base(t); set_delta(&in, 2.0);
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
        }
        { pl_in in = base(t); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(t - after_gap == 280 && o.state == PL_LOCKED && o.why == PL_OK);
    }
    /* (b) near for 120 ms, one hidden tick (deck 2 stopped), then near again */
    {
        pl_sm sm = {0};
        uint32_t t = START;
        for (int k = 0; k < 4; k++, t += TICK_MS) {
            pl_in in = base(t); set_delta(&in, 2.0);
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE);
        }
        { pl_in in = base(t); in.playing[1] = 0; set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_HIDDEN && o.why == PL_STOPPED_2);
        const uint32_t hidden_at = t;
        t += TICK_MS;
        for (int k = 0; k < 6; k++, t += TICK_MS) {   /* hidden_at+40 .. hidden_at+240 */
            pl_in in = base(t); set_delta(&in, 2.0);
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
        }
        CHECK(t - hidden_at == 280);
        /* the hold started at hidden_at + 40 (first near tick after the hidden one): 250 ms
           after that is hidden_at + 290, so hidden_at + 280 is still short and +320 locks */
        { pl_in in = base(t); set_delta(&in, 2.0); o = run(&sm, in); t += TICK_MS; }
        CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
        { pl_in in = base(t); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_LOCKED);
    }
    /* (c) t_ms from 0xFFFFFF00 across 0 in 40 ms ticks: a hold started before the wrap locks
       250 ms after it started */
    {
        pl_sm sm = {0};
        const uint32_t start = 0xFFFFFF00u;
        for (int k = 0; k <= 6; k++) {   /* start .. start+240 */
            pl_in in = base(start + (uint32_t)k * TICK_MS); set_delta(&in, 2.0);
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
        }
        CHECK(start + 7 * TICK_MS < start);   /* the clock has wrapped by the next tick */
        { pl_in in = base(start + 7 * TICK_MS); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_LOCKED && o.why == PL_OK);
        { pl_in in = base(start + 8 * TICK_MS); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_LOCKED && o.why == PL_OK);
    }
    /* (d) a shown sign with a tempo mismatch for 1000 ms, then a 30000 ms gap, mismatch still there */
    {
        pl_sm sm = {0};
        uint32_t t0 = 2000;
        { pl_in in = base(t0); set_delta(&in, 10.0); o = run(&sm, in); }
        CHECK(o.state == PL_NUDGE);
        for (uint32_t t = t0 + TICK_MS; t <= t0 + 1000; t += TICK_MS) {
            pl_in in = base(t); set_delta(&in, 10.0); in.bpm[1] = 126.0;
            o = run(&sm, in);
            CHECK(o.state == PL_NUDGE && o.why == PL_APART);
        }
        { pl_in in = base(t0 + 1000 + 30000); set_delta(&in, 10.0); in.bpm[1] = 126.0; o = run(&sm, in); }
        CHECK(o.state == PL_HIDDEN && o.why == PL_TEMPO_DIFF);
    }
    /* the gap is "more than TICK_GAP_MS": exactly TICK_GAP_MS later is still the same run */
    {
        pl_sm sm = {0};
        { pl_in in = base(1000); set_delta(&in, 2.0); o = run(&sm, in); }
        { pl_in in = base(1000 + TICK_GAP_MS); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_LOCKED);
        { pl_in in = base(1000 + 2 * TICK_GAP_MS + 1); set_delta(&in, 2.0); o = run(&sm, in); }
        CHECK(o.state == PL_NUDGE && o.why == PL_HOLDING);
    }
}

static void test_half_tempo_hidden(void)
{   /* bpm {64, 128}, ph equal -> HIDDEN, PL_TEMPO_DIFF */
    pl_sm sm = {0};
    pl_in in = base(1000);
    in.bpm[0] = 64.0; in.bpm[1] = 128.0;
    pl_out o = run(&sm, in);
    CHECK(o.state == PL_HIDDEN && o.why == PL_TEMPO_DIFF);
}

int main(void)
{
    test_delta();
    test_dx_and_number();
    test_gate_order();
    test_lock_hold_and_hysteresis();
    test_tempo_hold();
    test_stop_while_locked();
    test_bad_readings();
    test_tick_gap_and_wrap();
    test_half_tempo_hidden();
    printf("test_phase: %d/%d checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
