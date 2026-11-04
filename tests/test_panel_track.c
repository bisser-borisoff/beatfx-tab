/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests for panel_track: the touch positions are the ones recorded on the deck
 * (stock tab at 1250,268; the open panel's handle at 1067,266). */
#include <stdio.h>

#include "panel_track.h"

static int g_fail, g_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_fail++;                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                \
    } while (0)

static void test_tab_opens_and_handle_closes(void)
{
    CHECK(pt_on_touch(PT_CLOSED, 1250, 268) == PT_OPEN);
    CHECK(pt_on_touch(PT_OPEN, 1067, 266) == PT_CLOSED);
    CHECK(pt_on_touch(PT_UNKNOWN, 1250, 268) == PT_OPEN);
    CHECK(pt_on_touch(PT_UNKNOWN, 1067, 266) == PT_CLOSED);
}

static void test_assignment_is_absolute(void)
{
    CHECK(pt_on_touch(PT_OPEN, 1250, 268) == PT_OPEN);
    CHECK(pt_on_touch(PT_CLOSED, 1067, 266) == PT_CLOSED);
}

static void test_extended_strip_does_not_open(void)
{
    CHECK(pt_on_touch(PT_CLOSED, 1150, 250) == PT_CLOSED);
    CHECK(pt_on_touch(PT_UNKNOWN, 1150, 250) == PT_UNKNOWN);
}

static void test_other_touches_change_nothing(void)
{
    const int states[] = { PT_UNKNOWN, PT_CLOSED, PT_OPEN };
    for (int i = 0; i < 3; i++) CHECK(pt_on_touch(states[i], 600, 400) == states[i]);
}

/* Final review, Important 3: wrongly recording "closed" puts the label over the open panel,
 * so the handle zone stays tight around the recorded taps (1067,266) and (1068,255). */
static void test_open_panel_content_near_the_handle_does_not_close(void)
{
    CHECK(pt_on_touch(PT_OPEN, 1095, 310) == PT_OPEN);
    CHECK(pt_on_touch(PT_OPEN, 1040, 250) == PT_OPEN);
    CHECK(pt_on_touch(PT_OPEN, 1068, 255) == PT_CLOSED);
}

static void test_region_edges(void)
{
    CHECK(pt_on_touch(PT_CLOSED, 1180, 215) == PT_OPEN);
    CHECK(pt_on_touch(PT_CLOSED, 1179, 268) == PT_CLOSED);
    CHECK(pt_on_touch(PT_CLOSED, 1250, 296) == PT_CLOSED);
}

int main(void)
{
    test_tab_opens_and_handle_closes();
    test_assignment_is_absolute();
    test_extended_strip_does_not_open();
    test_other_touches_change_nothing();
    test_open_panel_content_near_the_handle_does_not_close();
    test_region_edges();
    printf("test_panel_track: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
