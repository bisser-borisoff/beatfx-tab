/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for fx_decode: the values come from two captures on Бисер's XDJ-AZ
 * (firmware 2.01, AZ-MODS 1.2.7), see the spec, section 4. */
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

static void test_names_and_groups(void)
{
    CHECK(streq(fxd_name(9), "TRIPLET FILTER"));
    CHECK(streq(fxd_short_name(9), "TRI FILTER"));
    CHECK(streq(fxd_short_name(12), "TRI ROLL"));
    CHECK(streq(fxd_short_name(1), "ECHO"));
    CHECK(streq(fxd_name(0), "DELAY") && streq(fxd_name(13), "MOBIUS"));
    for (int e = 0; e <= 4; e++) CHECK(fxd_group(e) == FXD_BLUE);
    CHECK(fxd_group(5) == FXD_ORANGE);
    for (int e = 6; e <= 10; e++) CHECK(fxd_group(e) == FXD_YELLOW);
    CHECK(fxd_group(11) == FXD_GREEN && fxd_group(12) == FXD_GREEN);
    CHECK(fxd_group(13) == FXD_PURPLE);
}

int main(void)
{
    test_beats_at_120_bpm();
    test_echo_three_quarters_at_128();
    test_reverb_shows_length_percent();
    test_on_bit_and_xpad_zone();
    test_invalid_reports_show_nothing();
    test_names_and_groups();
    printf("test_fx_decode: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
