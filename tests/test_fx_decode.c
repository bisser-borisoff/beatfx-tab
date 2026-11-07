/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for fx_decode: the values come from two captures on b.sr's XDJ-AZ
 * (firmware 2.01, AZ-MODS 1.2.7). */
#include <stdio.h>
#include <string.h>

#include "fx_decode.h"

static int g_fail, g_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_fail++;                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                \
    } while (0)

static unsigned char g_frame[128];

/* A mixer frame with the Beat FX report bytes set; TIME and BPM are little endian. */
static const unsigned char *mk(int m54, int m55, int m56, int bpm10, int val)
{
    memset(g_frame, 0, sizeof g_frame);
    g_frame[0x54] = (unsigned char)m54;
    g_frame[0x55] = (unsigned char)m55;
    g_frame[0x56] = (unsigned char)m56;
    g_frame[0x58] = (unsigned char)(bpm10 & 0xff);
    g_frame[0x59] = (unsigned char)(bpm10 >> 8);
    g_frame[0x5a] = (unsigned char)(val & 0xff);
    g_frame[0x5b] = (unsigned char)(val >> 8);
    return g_frame;
}

static fxd_state dec(const unsigned char *f, int len)
{
    return fxd_decode(fxd_pack(f, len));
}

static int streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static void test_beats_at_120_bpm(void)
{
    static const struct { int m55, m56, val; const char *beat; } T[] = {
        { 0x10, 0x01, 31, "1/16" }, { 0x10, 0x03, 62, "1/8" },  { 0x10, 0x05, 125, "1/4" },
        { 0xc0, 0x06, 166, "1/3" }, { 0x10, 0x07, 250, "1/2" }, { 0x10, 0x08, 333, "2/3" },
        { 0x10, 0x09, 375, "3/4" }, { 0x10, 0x0a, 500, "1" },   { 0x90, 0x0b, 666, "4/3" },
        { 0x10, 0x0c, 1000, "2" },  { 0x60, 0x0e, 2000, "4" },  { 0x10, 0x10, 4000, "8" },
    };
    for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
        fxd_state s = dec(mk(0x01, T[i].m55, T[i].m56, 1200, T[i].val), 128);
        CHECK(s.valid == 1);
        CHECK(s.effect == T[i].m55 >> 4);
        CHECK(s.percent == -1);
        CHECK(streq(s.beat, T[i].beat));
        if (!streq(s.beat, T[i].beat))
            printf("  row %zu: want %s, got %s\n", i, T[i].beat, s.beat ? s.beat : "(null)");
    }
}

static void test_echo_three_quarters_at_128(void)
{
    fxd_state s = dec(mk(0x01, 0x10, 0x09, 1280, 351), 128);
    CHECK(s.valid == 1);
    CHECK(s.effect == 1);
    CHECK(streq(s.beat, "3/4"));
    CHECK(s.on == 0);
    CHECK(s.percent == -1);
}

static void test_reverb_shows_length_percent(void)
{
    fxd_state a = dec(mk(0x01, 0x51, 0x32, 1200, 50), 128);
    fxd_state b = dec(mk(0x01, 0x56, 0x32, 1200, 50), 128);
    CHECK(a.valid == 1 && a.effect == 5 && a.percent == 50 && a.beat == NULL);
    CHECK(b.valid == 1 && b.effect == 5 && b.percent == 50 && b.beat == NULL);
}

static void test_on_bit_and_xpad_zone(void)
{
    CHECK(dec(mk(0x81, 0x10, 0x09, 1280, 351), 128).on == 1);
    CHECK(dec(mk(0x01, 0x10, 0x09, 1280, 351), 128).on == 0);
    fxd_state x = dec(mk(0x81, 0x13, 0x0a, 1200, 500), 128); /* ECHO, X-PAD zone 3 held */
    CHECK(x.valid == 1 && x.effect == 1 && x.on == 1 && streq(x.beat, "1"));
}

/* v0.1.4: a finger on the X-PAD sets 0x1c to 1 and 0x1d to its position (0xff with no finger),
 * in the same frame that turns the effect on (capture 2026-11-04: ECHO zones 1-6, REVERB,
 * FLANGER). The ON button leaves both as they are. Only both together count. */
static const unsigned char *finger(const unsigned char *f, int m1c, int m1d)
{
    g_frame[0x1c] = (unsigned char)m1c;
    g_frame[0x1d] = (unsigned char)m1d;
    return f;
}

static void test_xpad_finger_is_told_from_the_on_button(void)
{
    fxd_state s = dec(finger(mk(0x81, 0x11, 0x0a, 1200, 500), 0x01, 0x05), 128);
    CHECK(s.valid && s.on && s.xpad && s.effect == 1 && streq(s.beat, "1"));

    s = dec(finger(mk(0x81, 0x10, 0x0a, 1200, 500), 0x00, 0xff), 128); /* ON button */
    CHECK(s.valid && s.on && !s.xpad);

    s = dec(finger(mk(0x81, 0x56, 40, 1200, 40), 0x01, 0x00), 128); /* REVERB, position 0 */
    CHECK(s.valid && s.on && s.xpad && s.percent == 40);

    /* the two bytes disagree: not sure, so not the X-PAD */
    CHECK(!dec(finger(mk(0x81, 0x11, 0x0a, 1200, 500), 0x01, 0xff), 128).xpad);
    CHECK(!dec(finger(mk(0x81, 0x11, 0x0a, 1200, 500), 0x00, 0x05), 128).xpad);
    CHECK(!dec(finger(mk(0x81, 0x11, 0x0a, 1200, 500), 0x02, 0x05), 128).xpad);

    /* a finger with the effect silent (never seen): not shown as the X-PAD */
    s = dec(finger(mk(0x01, 0x11, 0x0a, 1200, 500), 0x01, 0x05), 128);
    CHECK(s.valid && !s.on && !s.xpad);

    CHECK(fxd_decode(0).xpad == 0);
}

static void test_invalid_reports_show_nothing(void)
{
    CHECK(fxd_pack(mk(0x01, 0x10, 0x09, 1280, 351), 0x5b) == 0); /* too short */
    CHECK(fxd_pack(mk(0x01, 0x10, 0x09, 1280, 351), 0) == 0);
    CHECK(fxd_pack(NULL, 128) == 0);
    CHECK(fxd_decode(0).valid == 0);
    CHECK(dec(mk(0x01, 0xe0, 0x0a, 1200, 500), 128).valid == 0); /* knob index 14 */
    CHECK(dec(mk(0x01, 0xf0, 0x0a, 1200, 500), 128).valid == 0); /* knob index 15 */
    CHECK(dec(mk(0x01, 0x10, 0x0a, 0, 500), 128).valid == 0);    /* no BPM */
    CHECK(dec(mk(0x01, 0x60, 0x0e, 1200, 208), 128).valid == 0); /* 0.416 beats: no match */
}

/* Final review, Important 2: if the deck caps TIME (Pioneer: about 4000 ms), TIME alone would
 * show a confident wrong beat. The beat index at 0x56 must agree with the matched value. */
static void test_capped_time_never_shows_a_wrong_beat(void)
{
    /* ECHO at 16 beats, 120 BPM, TIME capped at 4000 ms: TIME says 8, the index does not */
    CHECK(dec(mk(0x01, 0x10, 0x11, 1200, 4000), 128).valid == 0);
    /* ECHO at 8 beats (index 10), 80 BPM, TIME capped at 4000 ms: TIME says 16/3 */
    CHECK(dec(mk(0x01, 0x10, 0x10, 800, 4000), 128).valid == 0);
    /* index and TIME disagree on a plain value: 1 beat by TIME, index of 2 */
    CHECK(dec(mk(0x01, 0x10, 0x0c, 1200, 500), 128).valid == 0);
}

static void test_values_without_an_observed_index_still_show(void)
{
    /* TRIPLET ROLL 1/6 at 120 BPM: 83 ms, index not yet observed (not one of the known) */
    fxd_state s = dec(mk(0x01, 0xc0, 0x04, 1200, 83), 128);
    CHECK(s.valid == 1 && streq(s.beat, "1/6"));
}

static void test_names_and_groups(void)
{
    CHECK(streq(fxd_name(9), "TRIPLET FILTER"));
    CHECK(streq(fxd_short_name(9), "TRI FILTER"));
    CHECK(streq(fxd_short_name(12), "TRI ROLL"));
    CHECK(streq(fxd_short_name(1), "ECHO"));
    CHECK(streq(fxd_name(0), "DELAY") && streq(fxd_name(13), "MOBIUS"));
    for (int e = 0; e <= 4; e++) CHECK(fxd_group(e) == FXD_BLUE);
    CHECK(fxd_group(5) == FXD_ORANGE);
    for (int e = 6; e <= 10; e++) CHECK(fxd_group(e) == FXD_LIME);
    CHECK(fxd_group(11) == FXD_GREEN && fxd_group(12) == FXD_GREEN);
    CHECK(fxd_group(13) == FXD_PURPLE);
}

/* v1.1: CH SELECT, the eight buttons (3 1 2 4 / MIC CF.A CF.B MST). From an FX Probe round
 * on b.sr's deck (2026-11-06, a MARK before each press): 0x5d holds the selection, 0x50 + the
 * button in the order 1 2 3 4 MIC CF.A CF.B MST; after a power-on it was 0x58 with the MST
 * light on (MST again; a press on MST gives 0x57). The upper half was 5 in every frame, so
 * anything else is not trusted. 0x54's low bits change with the effect, not with CH SELECT. */
static fxd_state dec5d(int m54, int m5d)
{
    mk(m54, 0x10, 0x0a, 1200, 500);
    g_frame[0x5d] = (unsigned char)m5d;
    return dec(g_frame, 128);
}

static void test_ch_select_from_0x5d(void)
{
    static const struct { int m5d, ch; } T[] = {
        { 0x50, FXD_CH_1 },   { 0x51, FXD_CH_2 },   { 0x52, FXD_CH_3 },   { 0x53, FXD_CH_4 },
        { 0x54, FXD_CH_MIC }, { 0x55, FXD_CH_CFA }, { 0x56, FXD_CH_CFB }, { 0x57, FXD_CH_MST },
        { 0x58, FXD_CH_MST },
    };
    for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
        fxd_state s = dec5d(0x01, T[i].m5d);
        CHECK(s.valid == 1 && s.on == 0 && s.ch == T[i].ch);
        CHECK(s.ch_code == (T[i].m5d & 0x0f));
    }
    /* the effect's own bits in 0x54 do not move it */
    fxd_state s = dec5d(0x85, 0x52);
    CHECK(s.on == 1 && s.ch == FXD_CH_3);
    s = dec5d(0x03, 0x52);
    CHECK(s.ch == FXD_CH_3);
}

static void test_ch_select_unknown_values_show_no_channel(void)
{
    fxd_state s;
    const int bad[] = { 0x59, 0x5f, 0x00, 0x02, 0x42, 0x62, 0xd2, 0xff };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        s = dec5d(0x01, bad[i]);
        CHECK(s.ch == FXD_CH_UNKNOWN);
        /* a 0x5_ value keeps its code (shown as unknown); anything else is no code at all */
        CHECK(s.ch_code == ((bad[i] >> 4) == 0x5 ? (bad[i] & 0x0f) : -1));
        CHECK(s.valid == 1 && streq(s.beat, "1")); /* never hides the label */
    }
    /* an unusable report has no channel either: ch_code -1, not 0 (which would be CH 1) */
    s = fxd_decode(fxd_pack(mk(0x01, 0xf0, 0x0a, 1200, 500), 128));
    CHECK(s.valid == 0 && s.ch == FXD_CH_UNKNOWN && s.ch_code == -1);
    CHECK(fxd_decode(0).ch_code == -1);

    /* a frame that ends before 0x5d: the label shows, without a channel */
    mk(0x01, 0x10, 0x0a, 1200, 500);
    g_frame[0x5d] = 0x52;
    s = dec(g_frame, 0x5d);
    CHECK(s.valid == 1 && s.ch == FXD_CH_UNKNOWN && s.ch_code == -1);
}

/* v1.1: a CH SELECT press puts the selected channel's BPM into the report but keeps the TIME
 * worked out for the old one (FX Probe, 2026-11-06: CH 1 moved 120.0 to 124.0, TIME stayed
 * 500 ms for 39 s, 0x56 stayed 0x0a). A power-on starts at ECHO, BEAT 1, 120.0, 500 ms. */
static uint64_t pk(int m54, int m55, int m56, int bpm10, int val, int m5d)
{
    mk(m54, m55, m56, bpm10, val);
    g_frame[0x5d] = (unsigned char)m5d;
    return fxd_pack(g_frame, 128);
}

static void test_stale_time_edge_of_the_plain_check(void)
{
    /* TIME 500 ms from 120.0 BPM: the plain check passes within 6% of 1 beat */
    const int pass[] = { 1140, 1200, 1240, 1270 }, fail[] = { 1130, 1280, 1290, 1300 };
    for (size_t i = 0; i < 4; i++) {
        CHECK(fxd_decode(pk(0x01, 0x10, 0x0a, pass[i], 500, 0x50)).valid == 1);
        CHECK(fxd_decode(pk(0x01, 0x10, 0x0a, fail[i], 500, 0x51)).valid == 0);
    }
}

static void test_ch_select_keeps_the_confirmed_beat(void)
{
    uint64_t c = 0;
    fxd_state s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1200, 500, 0x58)); /* power-on */
    CHECK(s.valid == 1 && streq(s.beat, "1") && s.ch == FXD_CH_MST);
    CHECK(c != 0);
    /* CH 2 on a 128.0 deck: BPM moved, TIME did not; the beat is still 1 */
    s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 500, 0x51));
    CHECK(s.valid == 1 && s.effect == 1 && streq(s.beat, "1") && s.ch == FXD_CH_2);
    CHECK(s.on == 0 && s.percent == -1);
    /* CH 3 at 130.0 while the effect sounds, CH 4 at 113.0 */
    s = fxd_decode_held(&c, pk(0x81, 0x10, 0x0a, 1300, 500, 0x52));
    CHECK(s.valid == 1 && streq(s.beat, "1") && s.ch == FXD_CH_3 && s.on == 1);
    s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1130, 500, 0x53));
    CHECK(s.valid == 1 && streq(s.beat, "1") && s.ch == FXD_CH_4 && s.on == 0);
    /* a channel with no BPM (b.sr, 2026-11-07: every button other than MST and CH 1 hid the
     * label at the start of a set): only the BPM moved, so the beat is still 1 */
    s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 0, 500, 0x54));
    CHECK(s.valid == 1 && streq(s.beat, "1") && s.ch == FXD_CH_MIC);
    /* a finger on the X-PAD (0x55's low half moves, 0x1c/0x1d) while held */
    mk(0x81, 0x13, 0x0a, 1280, 500);
    g_frame[0x5d] = 0x51;
    g_frame[0x1c] = 0x01;
    g_frame[0x1d] = 0x05;
    s = fxd_decode_held(&c, fxd_pack(g_frame, 128));
    CHECK(s.valid == 1 && streq(s.beat, "1") && s.on == 1 && s.xpad == 1);
}

static void test_the_hold_needs_the_same_effect_beat_index_and_time(void)
{
    uint64_t c = 0;
    fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1200, 500, 0x58));
    /* BEAT pressed (another index) with the old TIME */
    CHECK(fxd_decode_held(&c, pk(0x01, 0x10, 0x0c, 1280, 500, 0x51)).valid == 0);
    /* another effect (DELAY) */
    CHECK(fxd_decode_held(&c, pk(0x01, 0x00, 0x0a, 1280, 500, 0x51)).valid == 0);
    /* another TIME, also one that would read 1 at the confirmed 120.0 (520 ms: 1.04) */
    CHECK(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 600, 0x51)).valid == 0);
    CHECK(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 520, 0x51)).valid == 0);
    /* an unknown knob position, and no report */
    CHECK(fxd_decode_held(&c, pk(0x01, 0xf0, 0x0a, 1280, 500, 0x51)).valid == 0);
    CHECK(fxd_decode_held(&c, 0).valid == 0);
    /* back on the confirmed effect, index and TIME: the same beat, so it shows again */
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 500, 0x51)).beat, "1"));
}

static void test_no_hold_without_a_confirmed_beat(void)
{
    /* e.g. a Reload while the report is already stale: the label stays hidden as before */
    uint64_t c = 0;
    CHECK(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 500, 0x51)).valid == 0);
    CHECK(c == 0);
}

static void test_a_new_confirmed_beat_replaces_the_old(void)
{
    uint64_t c = 0;
    fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1200, 500, 0x58)); /* 1 at 120.0 */
    fxd_decode_held(&c, pk(0x01, 0x10, 0x07, 1280, 234, 0x51)); /* 1/2 at 128.0 */
    /* the old 1 / 500 ms is no longer held */
    CHECK(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1300, 500, 0x51)).valid == 0);
    /* the new one is: 234 ms at 120.0 is 0.468 beats, outside the check */
    CHECK(fxd_decode(pk(0x01, 0x10, 0x07, 1200, 234, 0x50)).valid == 0);
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x10, 0x07, 1200, 234, 0x50)).beat, "1/2"));
}

static void test_reverb_on_the_way_keeps_the_held_beat(void)
{
    uint64_t c = 0;
    fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1200, 500, 0x58));
    /* the FX knob over REVERB (LENGTH 50%) and back to ECHO */
    fxd_state s = fxd_decode_held(&c, pk(0x01, 0x50, 0x32, 1280, 50, 0x51));
    CHECK(s.valid == 1 && s.effect == 5 && s.percent == 50);
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 500, 0x51)).beat, "1"));
}

/* Review, 2026-11-07: a value with no observed index (1/6, 8/3, 16/3, 16, 32, 64) took any
 * index that no observed value uses. A TIME read at about twice the BPM then showed 8/3 as
 * 16/3 (1.0.0 too), and the hold kept it. Every observed index runs in value order, so such
 * a value takes only an index between its observed neighbours: 8/3 0x0d, 16/3 0x0f. */
static void test_unobserved_values_need_an_index_between_their_neighbours(void)
{
    /* TRIPLET FILTER at 87.0: 8/3 is 1839 ms, 16/3 is 3678 ms */
    CHECK(streq(fxd_decode(pk(0x01, 0x90, 0x0d, 870, 1839, 0x50)).beat, "8/3"));
    CHECK(streq(fxd_decode(pk(0x01, 0x90, 0x0f, 870, 3678, 0x50)).beat, "16/3"));
    /* 8/3's TIME at 174.0 reads 16/3; 16/3's TIME from 174.0 read at 87.0 reads 8/3 */
    CHECK(fxd_decode(pk(0x01, 0x90, 0x0d, 1740, 1839, 0x51)).valid == 0);
    CHECK(fxd_decode(pk(0x01, 0x90, 0x0f, 870, 1839, 0x51)).valid == 0);
    /* 1/6 only with 0x04 (between 1/8's 0x03 and 1/4's 0x05) */
    CHECK(fxd_decode(pk(0x01, 0xc0, 0x02, 1200, 83, 0x50)).valid == 0);
    CHECK(streq(fxd_decode(pk(0x01, 0xc0, 0x04, 1200, 83, 0x50)).beat, "1/6"));
    /* 16 above the last observed value, 8 (0x10) */
    CHECK(fxd_decode(pk(0x01, 0x10, 0x10, 2400, 4000, 0x50)).valid == 0);
    CHECK(streq(fxd_decode(pk(0x01, 0x10, 0x11, 2400, 4000, 0x50)).beat, "16"));
}

static void test_ch_select_at_double_tempo_keeps_the_true_beat(void)
{
    const int bpm[] = { 1740, 1280, 1844, 1640, 600, 2000, 870 };
    uint64_t c = 0;
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x90, 0x0d, 870, 1839, 0x50)).beat, "8/3"));
    for (size_t i = 0; i < sizeof bpm / sizeof *bpm; i++)
        CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x90, 0x0d, bpm[i], 1839, 0x51)).beat, "8/3"));
    c = 0;
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x90, 0x0f, 1740, 1839, 0x50)).beat, "16/3"));
    for (size_t i = 0; i < sizeof bpm / sizeof *bpm; i++)
        CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x90, 0x0f, bpm[i], 1839, 0x51)).beat, "16/3"));
}

/* ECHO 8 at 120.0 is 4000 ms; at 80.0 the same TIME reads 16/3 and the index says no. The
 * hold shows 8 again, which is the beat set (the capped-TIME rule still holds without it). */
static void test_hold_across_a_tempo_that_reads_another_value(void)
{
    uint64_t c = 0;
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x10, 0x10, 1200, 4000, 0x58)).beat, "8"));
    CHECK(fxd_decode(pk(0x01, 0x10, 0x10, 800, 4000, 0x51)).valid == 0);
    CHECK(streq(fxd_decode_held(&c, pk(0x01, 0x10, 0x10, 800, 4000, 0x51)).beat, "8"));
}

/* A model of the deck for every value in the table: the index ladder in value order (the
 * observed indices, the gaps for the rest, 16 32 64 after 8), TIME in whole ms, uncapped or
 * capped at 4000, 16000 or 32000 ms (Pioneer's documented limits, not measured on the AZ).
 * A beat is set at one BPM, then CH SELECT moves the BPM twice and TIME stays, over 60-300
 * BPM. The label shows that beat or nothing, with one exception it inherits from 1.0.0:
 * 16, 32 and 64 share every index past 8's, so a TIME read at 2x or 4x the BPM swaps them in
 * the plain check too. The hold must never add a wrong value of its own (review, 2026-11-07:
 * it kept a swapped 16/32/64 at a BPM where the plain check hides). */
static void test_never_a_wrong_beat_over_ch_select_sweeps(void)
{
    static const struct { const char *s; double v; int idx; } L[] = {
        { "1/16", 1.0 / 16, 0x01 }, { "1/8", 1.0 / 8, 0x03 }, { "1/6", 1.0 / 6, 0x04 },
        { "1/4", 1.0 / 4, 0x05 },   { "1/3", 1.0 / 3, 0x06 }, { "1/2", 1.0 / 2, 0x07 },
        { "2/3", 2.0 / 3, 0x08 },   { "3/4", 3.0 / 4, 0x09 }, { "1", 1, 0x0a },
        { "4/3", 4.0 / 3, 0x0b },   { "2", 2, 0x0c },         { "8/3", 8.0 / 3, 0x0d },
        { "4", 4, 0x0e },           { "16/3", 16.0 / 3, 0x0f }, { "8", 8, 0x10 },
        { "16", 16, 0x11 },         { "32", 32, 0x12 },       { "64", 64, 0x13 },
    };
    static const double CAP[] = { 1e9, 4000, 16000, 32000 };
    long added = 0, wrong_small = 0, held = 0;
    for (size_t k = 0; k < sizeof CAP / sizeof *CAP; k++)
    for (size_t i = 0; i < sizeof L / sizeof *L; i++) {
        int big = L[i].v >= 16; /* 16 32 64: the plain check can swap them */
        for (int b0 = 600; b0 <= 3000; b0 += 40) {
            double ms = L[i].v * 600000.0 / b0;
            int t = ms > CAP[k] ? (int)CAP[k] : (int)(ms + 0.5);
            if (t > 0xffff) continue; /* TIME is two bytes */
            uint64_t c0 = 0;
            int bpm[3] = { b0, 0, 0 };
            for (int b1 = 600; b1 <= 3000; b1 += 40)
                for (int b2 = 600; b2 <= 3000; b2 += 200) {
                    uint64_t c = c0;
                    bpm[1] = b1, bpm[2] = b2;
                    for (int n = 0; n < 3; n++) {
                        uint64_t p = pk(0x01, 0x10, L[i].idx, bpm[n], t, 0x50 + n);
                        fxd_state s = fxd_decode_held(&c, p), f = fxd_decode(p);
                        if (!s.valid || streq(s.beat, L[i].s)) {
                            if (s.valid && !f.valid) held++;
                            continue;
                        }
                        if (!big) wrong_small++;
                        if (!f.valid || !streq(f.beat, s.beat)) { /* not the plain check's */
                            if (!added)
                                printf("  %s, cap %.0f: %d -> %d -> %d BPM x10, %d ms, report %d shows %s\n",
                                       L[i].s, CAP[k], bpm[0], bpm[1], bpm[2], t, n, s.beat);
                            added++;
                        }
                    }
                }
        }
    }
    CHECK(wrong_small == 0); /* below 16 never a wrong value */
    CHECK(added == 0);       /* the hold adds no wrong value of its own */
    CHECK(held > 0);         /* and it does show the beat where the plain check could not */
    if (wrong_small || added) printf("  %ld wrong below 16, %ld added by the hold\n", wrong_small, added);
}

/* v1.1 (b.sr, 2026-11-07): the label must not vanish as if the mod had crashed. A known
 * effect whose beat is not confirmed comes back named, with TIME in ms for a grey box. */
static void test_unconfirmed_beat_keeps_the_effect_named_with_time(void)
{
    /* the TIME knob turned: ECHO 3/4 (0x09) at 123.0, TIME 407 ms (b.sr's photo of the panel) */
    fxd_state s = fxd_decode(pk(0x81, 0x10, 0x09, 1230, 407, 0x57));
    CHECK(s.valid == 0 && s.named == 1 && s.effect == 1 && s.ms == 407);
    CHECK(s.beat == NULL && s.percent == -1 && s.on == 1 && s.ch == FXD_CH_MST && s.ch_code == 7);
    /* with a finger on the X-PAD */
    mk(0x81, 0x13, 0x09, 1230, 407);
    g_frame[0x1c] = 0x01;
    g_frame[0x1d] = 0x05;
    s = fxd_decode(fxd_pack(g_frame, 128));
    CHECK(s.valid == 0 && s.named == 1 && s.xpad == 1);
    /* no BPM: TIME still shows */
    s = fxd_decode(pk(0x01, 0x10, 0x0a, 0, 500, 0x52));
    CHECK(s.valid == 0 && s.named == 1 && s.ms == 500 && s.ch == FXD_CH_3 && s.on == 0);
    /* no TIME: named, no ms */
    s = fxd_decode(pk(0x01, 0x10, 0x0a, 1200, 0, 0x50));
    CHECK(s.valid == 0 && s.named == 1 && s.ms == 0);
    /* REVERB with a LENGTH over 100: named; 0x5a holds no ms there */
    s = fxd_decode(pk(0x01, 0x50, 0x70, 1200, 112, 0x50));
    CHECK(s.valid == 0 && s.named == 1 && s.effect == 5 && s.ms == 0);
    /* confirmed values are named and carry no ms */
    s = fxd_decode(pk(0x01, 0x10, 0x09, 1230, 365, 0x57));
    CHECK(s.valid == 1 && s.named == 1 && s.ms == 0 && streq(s.beat, "3/4"));
    s = fxd_decode(pk(0x01, 0x50, 0x32, 1200, 50, 0x57));
    CHECK(s.valid == 1 && s.named == 1 && s.ms == 0 && s.percent == 50);
    uint64_t c = 0;
    fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1200, 500, 0x58));
    s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 500, 0x51));
    CHECK(s.valid == 1 && s.named == 1 && s.ms == 0);
    /* the TIME knob after a confirmed beat: not held, named with the new TIME */
    s = fxd_decode_held(&c, pk(0x01, 0x10, 0x0a, 1280, 407, 0x51));
    CHECK(s.valid == 0 && s.named == 1 && s.ms == 407);
    /* an unknown knob position and no report are not named */
    CHECK(fxd_decode(pk(0x01, 0xe0, 0x0a, 1200, 500, 0x50)).named == 0);
    CHECK(fxd_decode(pk(0x01, 0xf0, 0x0a, 1200, 500, 0x50)).named == 0);
    CHECK(fxd_decode(0).named == 0 && fxd_decode(0).ms == 0);
}

int main(void)
{
    test_unconfirmed_beat_keeps_the_effect_named_with_time();
    test_unobserved_values_need_an_index_between_their_neighbours();
    test_ch_select_at_double_tempo_keeps_the_true_beat();
    test_hold_across_a_tempo_that_reads_another_value();
    test_never_a_wrong_beat_over_ch_select_sweeps();
    test_stale_time_edge_of_the_plain_check();
    test_ch_select_keeps_the_confirmed_beat();
    test_the_hold_needs_the_same_effect_beat_index_and_time();
    test_no_hold_without_a_confirmed_beat();
    test_a_new_confirmed_beat_replaces_the_old();
    test_reverb_on_the_way_keeps_the_held_beat();
    test_ch_select_from_0x5d();
    test_ch_select_unknown_values_show_no_channel();
    test_beats_at_120_bpm();
    test_echo_three_quarters_at_128();
    test_reverb_shows_length_percent();
    test_on_bit_and_xpad_zone();
    test_xpad_finger_is_told_from_the_on_button();
    test_invalid_reports_show_nothing();
    test_capped_time_never_shows_a_wrong_beat();
    test_values_without_an_observed_index_still_show();
    test_names_and_groups();
    printf("test_fx_decode: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
