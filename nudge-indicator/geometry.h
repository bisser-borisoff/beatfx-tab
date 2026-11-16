/* SPDX-License-Identifier: GPL-3.0-only */
/* nudge-indicator: the two boxes, offsets inside the box, colours. */
#ifndef PL_GEOMETRY_H
#define PL_GEOMETRY_H

#define BOX_W 89
#define BOX_H 27
#define BOX_OFF_X 456   /* Phase Meter off: right of the deck's loop indicator */
#define BOX_OFF_Y 259
#define BOX_ON_X  42    /* Phase Meter on: left of the meter's rulers */
#define BOX_ON_Y  90
#define TICK_DX   15    /* the fixed (deck 1) tick's left edge, relative to the box */
#define TICK_W    5
#define TICK1_DY  2     /* deck 1's tick: rows 2..11 */
#define TICK2_DY  12    /* deck 2's tick: rows 12..21 */
#define TICK_H    10
#define NUM_DX    38
#define NUM_PX    17
#define NUM_DY    -1    /* top of the number's line box, 1 row above the box: the ink then sits on rows 5..18, centred on the ticks (measured in the emulator, 2026-11-15) */
#define UNIT_GAP  3
#define UNIT_PX   12
#define UNIT_DY   6     /* top of the "ms" line box; tuned in the emulator (Task 7) */
#define SYM_W     26    /* the tempo symbol: two circles of 16 px, their centres 10 px apart */
#define SYM_H     16
#define SYM_DX    4     /* centred on the fixed tick (x 15..19) */
#define SYM_DY    4     /* rows 4..19, centred on the two ticks (rows 2..21) */
#define WORD      "TEMPO"
#define WORD_DX   NUM_DX
#define WORD_PX   UNIT_PX
#define WORD_DY   3     /* top of the word's line box: its ink (9 rows of capitals) then sits on rows 8..16, centred on the symbol (measured in the emulator, 2026-11-16) */
#define COL_BG    0x000000
#define COL_TICK  0xE1E1E1
#define COL_NUM   0xE1E1E1
#define COL_UNIT  0x969696
#define COL_LOCK  0x2EE06A

/* the Status page (Mod Menu) */
#define PAGE_PX      20  /* text size of a page line */
#define PAGE_X       24  /* left margin inside the page area */
#define PAGE_TOP     16  /* gap under the header row */
#define PAGE_LINE_H  (PAGE_PX * 3 / 2 + 4)
#define PAGE_RIGHT   8   /* right margin: a longer line is cut */

#endif
