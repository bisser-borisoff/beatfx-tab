/* SPDX-License-Identifier: GPL-3.0-only
 *
 * fx_decode: the Beat FX report in AZ_BUS_MIXER_IN frames (measured on the deck from frame
 * captures while turning each control):
 *   0x54 bit 0x80  the effect is sounding (ON/OFF button or a finger on the X-PAD)
 *   0x55 >> 4      the effect, 0..13 in knob order (low nibble: X-PAD zone or a flag)
 *   0x56           beat index (REVERB: LENGTH %)
 *   0x58..0x59     BPM x 10, little endian
 *   0x5a..0x5b     TIME in ms (REVERB: LENGTH %), little endian
 *   0x5d           v1.1, CH SELECT: 0x50 + the button in the order 1 2 3 4 MIC CF.A CF.B MST
 *                  (0x58 after a power-on, with MST lit); see fxd_ch_of
 *   0x1c, 0x1d     a finger on the X-PAD: 0x1c = 1 and 0x1d = its position (0xff without one)
 *
 * fxd_pack copies one frame's report into a single word, so a reader on another thread
 * never sees a new effect with the old time. fxd_decode turns it into what the label shows;
 * a value it is not sure of comes back with valid = 0, never a wrong number (v1.1: with a
 * known effect also named = 1 and TIME in ms, for the grey box).
 */
#ifndef BEATFX_FX_DECODE_H
#define BEATFX_FX_DECODE_H

#include <stdint.h>

#define FXD_MIN_LEN 0x5c

enum { FXD_BLUE, FXD_ORANGE, FXD_LIME, FXD_GREEN, FXD_PURPLE };

/* The CH SELECT buttons: 3 1 2 4 above MIC CF.A CF.B MST. 0 = a code not known yet. */
enum { FXD_CH_UNKNOWN, FXD_CH_1, FXD_CH_2, FXD_CH_3, FXD_CH_4,
       FXD_CH_MIC, FXD_CH_CFA, FXD_CH_CFB, FXD_CH_MST };

typedef struct {
    int valid;        /* 0 = show nothing (stock look) */
    int effect;       /* 0..13, knob order */
    int on;           /* 1 = effect sounding (ON/OFF or X-PAD held) */
    int percent;      /* REVERB: LENGTH %, else -1 */
    const char *beat; /* static "3/4" etc.; NULL for REVERB or invalid */
    int xpad;         /* 1 = sounding through a finger on the X-PAD (not the ON button) */
    int ch;           /* FXD_CH_*: where the effect goes */
    int ch_code;      /* the low half of 0x5d (MST: 7, or 8 after a power-on); -1 when 0x5d
                       * is missing or not 0x5_ */
    int named;        /* v1.1: 1 = the effect is known (knob 0..13), with or without a value.
                       * valid = 0 with named = 1: the beat is not confirmed (the TIME knob
                       * turned, a BPM TIME does not fit); the label shows the name and ms */
    int ms;           /* with valid = 0 and named = 1: TIME in ms for the grey box, 0 = none
                       * (and always 0 when valid) */
} fxd_state;

uint64_t fxd_pack(const unsigned char *frame, int len); /* 0 = no usable data */
fxd_state fxd_decode(uint64_t packed);

/* v1.1: a CH SELECT press puts the selected channel's BPM into the report but keeps the TIME
 * worked out for the old one (FX Probe, 2026-11-06), so TIME x BPM can miss every known beat
 * while the beat itself did not change. fxd_decode_held keeps in *confirmed the last report
 * whose beat it confirmed (0 = none yet). It keeps only a beat its index names alone: never
 * REVERB, and never 16, 32 or 64, which share their indices. While a
 * report has the same effect, beat index (0x56) and TIME as that one, whatever its BPM (0 on
 * a channel with no BPM), it shows that confirmed beat; 0x54, 0x55's low half, the X-PAD finger and CH SELECT
 * come from the new report. One caller, one thread. */
fxd_state fxd_decode_held(uint64_t *confirmed, uint64_t packed);
const char *fxd_name(int effect);       /* as on the knob, e.g. "TRIPLET FILTER" */
const char *fxd_short_name(int effect); /* "TRI FILTER", "TRI ROLL", else fxd_name */
int fxd_group(int effect);              /* FXD_* */
int fxd_ch_of(int code);                /* FXD_CH_* for the low half of 0x5d */

#endif
