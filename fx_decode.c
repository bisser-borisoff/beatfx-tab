/* SPDX-License-Identifier: GPL-3.0-only */
#include "fx_decode.h"

#include <stddef.h>

enum { FX_N = 14, FX_REVERB = 5 };

static const char *const NAME[FX_N] = {
    "DELAY", "ECHO", "PING PONG", "SPIRAL", "HELIX", "REVERB", "FLANGER",
    "PHASER", "FILTER", "TRIPLET FILTER", "TRANS", "ROLL", "TRIPLET ROLL", "MOBIUS",
};

static const unsigned char GROUP[FX_N] = {
    FXD_BLUE,   FXD_BLUE,   FXD_BLUE,   FXD_BLUE,   FXD_BLUE,  FXD_ORANGE, FXD_YELLOW,
    FXD_YELLOW, FXD_YELLOW, FXD_YELLOW, FXD_YELLOW, FXD_GREEN, FXD_GREEN,  FXD_PURPLE,
};

/* The beat values the deck's effects use, smallest first. */
static const struct {
    double v;
    const char *s;
} FRAC[] = {
    { 1.0 / 16, "1/16" }, { 1.0 / 8, "1/8" },   { 1.0 / 6, "1/6" }, { 1.0 / 4, "1/4" },
    { 1.0 / 3, "1/3" },   { 1.0 / 2, "1/2" },   { 2.0 / 3, "2/3" }, { 3.0 / 4, "3/4" },
    { 1, "1" },           { 4.0 / 3, "4/3" },   { 2, "2" },         { 8.0 / 3, "8/3" },
    { 4, "4" },           { 16.0 / 3, "16/3" }, { 8, "8" },         { 16, "16" },
    { 32, "32" },         { 64, "64" },
};

#define SNAP_TOLERANCE 1.06 /* beats from TIME x BPM may be this far off a known value */
#define PACK_PRESENT (1ull << 63)

uint64_t fxd_pack(const unsigned char *f, int len)
{
    if (!f || len < FXD_MIN_LEN) return 0;
    return PACK_PRESENT | (uint64_t)f[0x54] | (uint64_t)f[0x55] << 8 | (uint64_t)f[0x56] << 16 |
           (uint64_t)(f[0x58] | f[0x59] << 8) << 24 | (uint64_t)(f[0x5a] | f[0x5b] << 8) << 40;
}

static const char *snap_beats(double beats)
{
    const char *best = NULL;
    double best_ratio = 0;
    for (size_t i = 0; i < sizeof FRAC / sizeof *FRAC; i++) {
        double r = beats > FRAC[i].v ? beats / FRAC[i].v : FRAC[i].v / beats;
        if (!best || r < best_ratio) {
            best = FRAC[i].s;
            best_ratio = r;
        }
    }
    return best_ratio <= SNAP_TOLERANCE ? best : NULL;
}

fxd_state fxd_decode(uint64_t p)
{
    fxd_state s = { 0, -1, 0, -1, NULL };
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
        s.beat = snap_beats((double)val * bpm10 / 600000.0);
        if (!s.beat) return s;
    }
    s.valid = 1;
    s.effect = effect;
    s.on = (m54 & 0x80) != 0;
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
