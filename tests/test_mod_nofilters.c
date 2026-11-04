/* SPDX-License-Identifier: GPL-3.0-only
 * A core without frame or touch filters (the SDK's harness is one): the mod stays loaded,
 * logs why, and never shows the label. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "azmod-mod.h"

static int g_fail, g_checks;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(cond)) {                                                                   \
            g_fail++;                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                \
    } while (0)

static int g_shown, g_logs;
static az_surface_v1 g_desc;

static void f_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; g_logs++; }
static az_surface *f_screate(const char *id, const az_surface_v1 *s) { (void)id; g_desc = *s; return (az_surface *)&g_desc; }
static void f_splace(az_surface *s, az_rect r) { (void)s; (void)r; }
static void f_sshow(az_surface *s, int on) { (void)s; g_shown |= on; }
static void f_sredraw(az_surface *s) { (void)s; }
static int f_view(void) { return AZ_VIEW_DECK; }
static int f_decks(void) { return 2; }

int main(void)
{
    az_core_v1 core;
    memset(&core, 0, sizeof core);
    core.abi = AZ_MOD_ABI;
    core.size = sizeof core;
    core.log = f_log;
    core.surface_create = f_screate; core.surface_place = f_splace;
    core.surface_show = f_sshow; core.surface_redraw = f_sredraw;
    core.view = f_view; core.deck_count = f_decks;

    const az_mod_v1 *m = az_mod_v1_entry();
    CHECK(m->init(&core) == 0);
    CHECK(g_logs > 0);
    for (int i = 0; i < 10; i++) {
        m->tick();
        usleep(10 * 1000);
    }
    CHECK(g_shown == 0);
    printf("test_mod_nofilters: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
