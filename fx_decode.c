/* SPDX-License-Identifier: GPL-3.0-only */
#include "fx_decode.h"

#include <stddef.h>

enum { FX_N = 14, FX_REVERB = 5 };

static const char *const NAME[FX_N] = {
    "DELAY", "ECHO", "PING PONG", "SPIRAL", "HELIX", "REVERB", "FLANGER",
    "PHASER", "FILTER", "TRIPLET FILTER", "TRANS", "ROLL", "TRIPLET ROLL", "MOBIUS",
};

static const unsigned char GROUP[FX_N] = {
    FXD_BLUE,   FXD_BLUE,   FXD_BLUE,   FXD_BLUE,   FXD_BLUE,  FXD_ORANGE, FXD_LIME,
    FXD_LIME, FXD_LIME, FXD_LIME, FXD_LIME, FXD_GREEN, FXD_GREEN,  FXD_PURPLE,
};

/* The beat values the deck's effects use, smallest first, with the beat index (0x56) seen
 * for each on the deck (-1 = not observed yet). */
static const struct {
    double v;
    const char *s;
    int idx;
} FRAC[] = {
    { 1.0 / 16, "1/16", 0x01 }, { 1.0 / 8, "1/8", 0x03 },    { 1.0 / 6, "1/6", -1 },
    { 1.0 / 4, "1/4", 0x05 },   { 1.0 / 3, "1/3", 0x06 },    { 1.0 / 2, "1/2", 0x07 },
    { 2.0 / 3, "2/3", 0x08 },   { 3.0 / 4, "3/4", 0x09 },    { 1, "1", 0x0a },
    { 4.0 / 3, "4/3", 0x0b },   { 2, "2", 0x0c },            { 8.0 / 3, "8/3", -1 },
    { 4, "4", 0x0e },           { 16.0 / 3, "16/3", -1 },    { 8, "8", 0x10 },
    { 16, "16", -1 },           { 32, "32", -1 },            { 64, "64", -1 },
};
#define N_FRAC (sizeof FRAC / sizeof *FRAC)

/* TIME and the index must agree: the deck may cap TIME (Pioneer: about 4000 ms), and then
 * TIME alone names a smaller value with full confidence. A value with an observed index
 * needs exactly that index. One without needs an index between those of its nearest
 * observed neighbours in the table (above 8: any index past 8's): every observed index runs
 * in value order, so 8/3 is only 0x0d, and a TIME read at twice the BPM (a CH SELECT press
 * keeps the old TIME) cannot pass 8/3 off as 16/3 (review, 2026-11-07). */
static void observed_neighbours(size_t i, int *lo, int *hi)
{
    *lo = -1, *hi = 0x100;
    for (size_t k = i; k-- > 0;)
        if (FRAC[k].idx >= 0) {
            *lo = FRAC[k].idx;
            break;
        }
    for (size_t k = i + 1; k < N_FRAC; k++)
        if (FRAC[k].idx >= 0) {
            *hi = FRAC[k].idx;
            break;
        }
}

static int index_agrees(size_t i, unsigned m56)
{
    if (FRAC[i].idx >= 0) return (unsigned)FRAC[i].idx == m56;
    int lo, hi;
    observed_neighbours(i, &lo, &hi);
    return (int)m56 > lo && (int)m56 < hi;
}

/* 1 when the beat index alone names the value: an observed index, or the single index
 * between two observed neighbours (1/6, 8/3, 16/3). 16, 32 and 64 share every index past
 * 8's, so a TIME read at 2x or 4x the BPM can swap them even in the plain check (1.0.0 too);
 * the hold never keeps them (review, 2026-11-07). */
static int index_pins(const char *beat)
{
    for (size_t i = 0; i < N_FRAC; i++)
        if (FRAC[i].s == beat) {
            if (FRAC[i].idx >= 0) return 1;
            int lo, hi;
            observed_neighbours(i, &lo, &hi);
            return hi - lo == 2;
        }
    return 0;
}

#define SNAP_TOLERANCE 1.06 /* beats from TIME x BPM may be this far off a known value */
#define PACK_PRESENT (1ull << 63)
#define PACK_FINGER (1ull << 56)
#define PACK_CH_OK (1ull << 61)   /* 0x5d was in the frame and read 0x5_ */
#define PACK_CH_SHIFT 57          /* its low half, bits 57..60 */

uint64_t fxd_pack(const unsigned char *f, int len)
{
    if (!f || len < FXD_MIN_LEN) return 0;
    /* a finger on the X-PAD: 0x1c is 1 and 0x1d holds its position (0xff with no finger) */
    uint64_t finger = f[0x1c] == 0x01 && f[0x1d] != 0xff ? PACK_FINGER : 0;
    /* CH SELECT: only a 0x5_ value is trusted (every frame seen on the deck) */
    uint64_t ch = len > 0x5d && (f[0x5d] >> 4) == 0x5
                      ? PACK_CH_OK | (uint64_t)(f[0x5d] & 0x0f) << PACK_CH_SHIFT
                      : 0;
    return PACK_PRESENT | finger | ch | (uint64_t)f[0x54] | (uint64_t)f[0x55] << 8 |
           (uint64_t)f[0x56] << 16 | (uint64_t)(f[0x58] | f[0x59] << 8) << 24 |
           (uint64_t)(f[0x5a] | f[0x5b] << 8) << 40;
}

/* The index in FRAC of the value nearest to beats, or -1 when none is within tolerance. */
static int snap_beats(double beats)
{
    int best = -1;
    double best_ratio = 0;
    for (size_t i = 0; i < N_FRAC; i++) {
        double r = beats > FRAC[i].v ? beats / FRAC[i].v : FRAC[i].v / beats;
        if (best < 0 || r < best_ratio) {
            best = (int)i;
            best_ratio = r;
        }
    }
    return best_ratio <= SNAP_TOLERANCE ? best : -1;
}

fxd_state fxd_decode(uint64_t p)
{
    fxd_state s = { 0, -1, 0, -1, NULL, 0, FXD_CH_UNKNOWN, -1, 0, 0 };
    if (!(p & PACK_PRESENT)) return s;
    unsigned m54 = (unsigned)(p & 0xff), m55 = (unsigned)(p >> 8 & 0xff), m56 = (unsigned)(p >> 16 & 0xff);
    unsigned bpm10 = (unsigned)(p >> 24 & 0xffff), val = (unsigned)(p >> 40 & 0xffff);
    int effect = (int)(m55 >> 4);
    if (effect >= FX_N) return s;

    /* v1.1: a known effect is named, with its state and channel, even when its value is not
     * confirmed; the label then shows TIME in ms instead of hiding (b.sr, 2026-11-07) */
    s.named = 1;
    s.effect = effect;
    s.on = (m54 & 0x80) != 0;
    s.xpad = s.on && (p & PACK_FINGER) != 0;
    s.ch_code = p & PACK_CH_OK ? (int)(p >> PACK_CH_SHIFT & 0x0f) : -1;
    s.ch = fxd_ch_of(s.ch_code);
    if (effect == FX_REVERB) {
        if (m56 > 100) return s; /* 0x5a is LENGTH here, not ms: no ms either */
        s.percent = (int)m56;
    } else {
        int i = bpm10 && val ? snap_beats((double)val * bpm10 / 600000.0) : -1;
        if (i < 0 || !index_agrees((size_t)i, m56)) {
            s.ms = (int)val;
            return s;
        }
        s.beat = FRAC[i].s;
    }
    s.valid = 1;
    return s;
}

#define PACK_BPM (0xffffull << 24)
/* what a held beat needs unchanged: the effect (0x55's high half), the beat index, TIME */
#define PACK_BEAT_KEY (0xf000ull | 0xff0000ull | 0xffffull << 40)

fxd_state fxd_decode_held(uint64_t *confirmed, uint64_t p)
{
    fxd_state s = fxd_decode(p);
    if (s.valid) {
        if (s.beat && index_pins(s.beat)) *confirmed = p; /* REVERB has no beat to hold */
        return s;
    }
    uint64_t c = *confirmed;
    if (!c || !(p & PACK_PRESENT) || (p & PACK_BEAT_KEY) != (c & PACK_BEAT_KEY)) return s;
    /* only the BPM moved (a channel with no BPM sends 0): decode with the BPM the beat was
     * confirmed at */
    return fxd_decode((p & ~PACK_BPM) | (c & PACK_BPM));
}

/* From an FX Probe round on b.sr's deck (2026-11-06): 0..7 = 1 2 3 4 MIC CF.A CF.B MST, in
 * press order; 8 = after a power-on, with the MST light on. */
int fxd_ch_of(int code)
{
    static const int CH[9] = { FXD_CH_1,   FXD_CH_2,   FXD_CH_3,   FXD_CH_4,  FXD_CH_MIC,
                               FXD_CH_CFA, FXD_CH_CFB, FXD_CH_MST, FXD_CH_MST };
    return code >= 0 && code <= 8 ? CH[code] : FXD_CH_UNKNOWN;
}

const char *fxd_name(int effect)
{
    return effect >= 0 && effect < FX_N ? NAME[effect] : "";
}

const char *fxd_short_name(int effect)
{
    if (effect == 9) return "TRI FILTER";
    if (effect == 12) return "TRI ROLL";
    return fxd_name(effect);
}

int fxd_group(int effect)
{
    return effect >= 0 && effect < FX_N ? GROUP[effect] : FXD_BLUE;
}
