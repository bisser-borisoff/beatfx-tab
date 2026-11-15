/* SPDX-License-Identifier: GPL-3.0-only */
/* Replay of the emulator recording (phase-probe) through pl_step: the rules against what the
 * emulated deck really reports. The ticks are tests/fixtures/replay.h, cut out by
 * tools/make-fixture.py. */
#include <stdio.h>

#include "fixtures/replay.h"
#include "phase.h"
#include "tunables.h"

static int total, passed;
#define CHECK(c) do { total++; if (c) passed++; else printf("FAIL: %s (line %d)\n", #c, __LINE__); } while (0)

#define SYNC_LOCK_WITHIN_MS 300   /* the plan: locked at or before this after the first tick */
#define APART_DX (-DX_MAX)        /* E2 sits about 46 ms apart: deck 2 ahead, the tick at the limit */
#define APART_MS_MIN 30
#define APART_MS_MAX 46

static pl_in tick_in(const struct replay_tick *r)
{
    pl_in in = {0};
    in.t_ms = r->t_ms;
    in.row_on = 1; in.theme = 0; in.deck_count = 2; in.deck34 = 0;
    in.loaded[0] = in.loaded[1] = 1;
    in.playing[0] = in.playing[1] = 1;
    in.grid_ok[0] = in.grid_ok[1] = 1;
    in.bpm[0] = r->bpm1; in.bpm[1] = r->bpm2;
    in.ph[0] = r->ph1; in.ph[1] = r->ph2;
    return in;
}

static void test_sync_locks_and_stays(void)
{
    pl_sm sm = {0};
    int first_locked = -1;
    for (int i = 0; i < REPLAY_SYNC_N; i++) {
        pl_in in = tick_in(&REPLAY_SYNC[i]);
        pl_out out;
        pl_step(&sm, &in, &out);
        if (out.state == PL_LOCKED && first_locked < 0) {
            first_locked = i;
            printf("sync: locked at tick %d, %u ms after the first, delta %.2f ms\n", i,
                   (unsigned)(in.t_ms - REPLAY_SYNC[0].t_ms), out.delta_ms);
        }
        if (first_locked >= 0 && out.state != PL_LOCKED)
            printf("sync: tick %d (+%u ms) is %d, why %d, delta %.2f ms\n", i,
                   (unsigned)(in.t_ms - REPLAY_SYNC[0].t_ms), (int)out.state, (int)out.why, out.delta_ms);
        CHECK(first_locked < 0 || out.state == PL_LOCKED);
    }
    CHECK(first_locked >= 0);
    if (first_locked >= 0)
        CHECK((uint32_t)(REPLAY_SYNC[first_locked].t_ms - REPLAY_SYNC[0].t_ms) <= SYNC_LOCK_WITHIN_MS);
}

static void test_apart_is_nudging(void)
{
    pl_sm sm = {0};
    for (int i = 0; i < REPLAY_APART_N; i++) {
        pl_in in = tick_in(&REPLAY_APART[i]);
        pl_out out;
        pl_step(&sm, &in, &out);
        int ok = out.state == PL_NUDGE && out.why == PL_APART && out.dx == APART_DX &&
                 out.ms >= APART_MS_MIN && out.ms <= APART_MS_MAX;
        if (!ok)
            printf("apart: tick %d state %d why %d dx %d ms %d delta %.2f\n", i, (int)out.state, (int)out.why,
                   out.dx, out.ms, out.delta_ms);
        CHECK(ok);
    }
}

static void test_diff_tempo_is_hidden(void)
{
    pl_sm sm = {0};
    for (int i = 0; i < REPLAY_DIFF_TEMPO_N; i++) {
        pl_in in = tick_in(&REPLAY_DIFF_TEMPO[i]);
        pl_out out;
        pl_step(&sm, &in, &out);
        int ok = out.state == PL_HIDDEN && out.why == PL_TEMPO_DIFF;
        if (!ok) printf("diff tempo: tick %d state %d why %d\n", i, (int)out.state, (int)out.why);
        CHECK(ok);
    }
}

int main(void)
{
    test_sync_locks_and_stays();
    test_apart_is_nudging();
    test_diff_tempo_is_hidden();
    printf("test_replay: %d/%d checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
