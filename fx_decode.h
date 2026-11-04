/* SPDX-License-Identifier: GPL-3.0-only
 *
 * fx_decode: the Beat FX report in AZ_BUS_MIXER_IN frames (measured on the deck from frame
 * captures while turning each control):
 *   0x54 bit 0x80  the effect is sounding (ON/OFF button or a finger on the X-PAD)
 *   0x55 >> 4      the effect, 0..13 in knob order (low nibble: X-PAD zone or a flag)
 *   0x56           beat index (REVERB: LENGTH %)
 *   0x58..0x59     BPM x 10, little endian
 *   0x5a..0x5b     TIME in ms (REVERB: LENGTH %), little endian
 *   0x1c, 0x1d     a finger on the X-PAD: 0x1c = 1 and 0x1d = its position (0xff without one)
 *
 * fxd_pack copies one frame's report into a single word, so a reader on another thread
 * never sees a new effect with the old time. fxd_decode turns it into what the label shows;
 * anything it is not sure of comes back with valid = 0 (show the stock tab, never a wrong
 * number).
 */
#ifndef BEATFX_FX_DECODE_H
#define BEATFX_FX_DECODE_H

#include <stdint.h>

#define FXD_MIN_LEN 0x5c

enum { FXD_BLUE, FXD_ORANGE, FXD_YELLOW, FXD_GREEN, FXD_PURPLE };

typedef struct {
    int valid;        /* 0 = show nothing (stock look) */
    int effect;       /* 0..13, knob order */
    int on;           /* 1 = effect sounding (ON/OFF or X-PAD held) */
    int percent;      /* REVERB: LENGTH %, else -1 */
    const char *beat; /* static "3/4" etc.; NULL for REVERB or invalid */
    int xpad;         /* 1 = sounding through a finger on the X-PAD (not the ON button) */
} fxd_state;

uint64_t fxd_pack(const unsigned char *frame, int len); /* 0 = no usable data */
fxd_state fxd_decode(uint64_t packed);
const char *fxd_name(int effect);       /* as on the knob, e.g. "TRIPLET FILTER" */
const char *fxd_short_name(int effect); /* "TRI FILTER", "TRI ROLL", else fxd_name */
int fxd_group(int effect);              /* FXD_* */

#endif
