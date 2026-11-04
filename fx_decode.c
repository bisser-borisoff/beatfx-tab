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
 * needs exactly that index; one without needs an index that belongs to no observed value. */
static int index_agrees(size_t i, unsigned m56)
{
    if (FRAC[i].idx >= 0) return (unsigned)FRAC[i].idx == m56;
    for (size_t k = 0; k < N_FRAC; k++)
        if (FRAC[k].idx >= 0 && (unsigned)FRAC[k].idx == m56) return 0;
    return 1;
}

#define SNAP_TOLERANCE 1.06 /* beats from TIME x BPM may be this far off a known value */
#define PACK_PRESENT (1ull << 63)
#define PACK_FINGER (1ull << 56)

uint64_t fxd_pack(const unsigned char *f, int len)
{
    if (!f || len < FXD_MIN_LEN) return 0;
    /* a finger on the X-PAD: 0x1c is 1 and 0x1d holds its position (0xff with no finger) */
    uint64_t finger = f[0x1c] == 0x01 && f[0x1d] != 0xff ? PACK_FINGER : 0;
    return PACK_PRESENT | finger | (uint64_t)f[0x54] | (uint64_t)f[0x55] << 8 |
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
    fxd_state s = { 0, -1, 0, -1, NULL, 0 };
    if (!(p & PACK_PRESENT)) return s;
    unsigned m54 = (unsigned)(p & 0xff), m55 = (unsigned)(p >> 8 & 0xff), m56 = (unsigned)(p >> 16 & 0xff);
    unsigned bpm10 = (unsigned)(p >> 24 & 0xffff), val = (unsigned)(p >> 40 & 0xffff);
    int effect = (int)(m55 >> 4);
    if (effect >= FX_N) return s;

    if (effect == FX_REVERB) {
        if (m56 > 100) return s;
        s.percent = (int)m56;
    } else {
        if (bpm10 == 0 || val == 0) return s;
        int i = snap_beats((double)val * bpm10 / 600000.0);
        if (i < 0 || !index_agrees((size_t)i, m56)) return s;
        s.beat = FRAC[i].s;
    }
    s.valid = 1;
    s.effect = effect;
    s.on = (m54 & 0x80) != 0;
    s.xpad = s.on && (p & PACK_FINGER) != 0;
    return s;
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
