/* SPDX-License-Identifier: GPL-3.0-only
 *
 * A fake core for the tests of nudge-indicator.c (test_mod.c): the services the mod calls, a simulated
 * UI clock (the mod's tick reads CLOCK_MONOTONIC through clock_gettime, as in the SDK's harness),
 * four fake decks with a beat phase, the Mod Menu rows with one official row (phase-meter), one
 * surface, and a recording of the draw helpers.
 *
 * One mod per process: the mod keeps its state in statics, so every test runs in a process of its
 * own (the tests' main forks and execs itself per test).
 */
#ifndef PL_MOD_FAKE_H
#define PL_MOD_FAKE_H

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#include "azmod-mod.h"
#include "geometry.h"
#include "phase.h"
#include "sign_draw.h"

#define UN __attribute__((unused))

/* ---- what the mod exports for the tests (nudge-indicator.c, PL_TEST) ---- */
int pl_test_out(pl_out *o);        /* the last pl_step result; returns 1 while the surface is shown */
int pl_test_sign(sign_state *s);   /* what paint would draw now (copied under the mutex) */

/* ---- checks ---- */
static int g_checks, g_fails;
#define CHECK(c)                                                                       \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(c)) {                                                                    \
            g_fails++;                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                        \
        }                                                                              \
    } while (0)

/* ---- the clock ---- */
static atomic_long g_sim_ms = 1000000; /* the UI thread's clock */

int clock_gettime(clockid_t id, struct timespec *ts)
{
    if (id == CLOCK_MONOTONIC) {
        long m = atomic_load(&g_sim_ms);
        ts->tv_sec = m / 1000;
        ts->tv_nsec = (m % 1000) * 1000000L;
        return 0;
    }
#if defined(__linux__)
    return (int)syscall(SYS_clock_gettime, id, ts);
#else
    uint64_t ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    ts->tv_sec = (time_t)(ns / 1000000000u);
    ts->tv_nsec = (long)(ns % 1000000000u);
    return 0;
#endif
}

/* ---- the log: only the main (UI) thread may call it ---- */
static pthread_t g_main;
static char g_logv[64][200];
static int g_nlog, g_log_bad;

static void f_log(const char *id, const char *fmt, ...)
{
    (void)id;
    if (!pthread_equal(pthread_self(), g_main)) g_log_bad++;
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_nlog < 64) memcpy(g_logv[g_nlog], line, sizeof line);
    g_nlog++;
}
UN static int log_count(const char *s)
{
    int n = 0;
    for (int i = 0; i < g_nlog && i < 64; i++)
        if (strstr(g_logv[i], s)) n++;
    return n;
}

/* ---- the decks ---- */
struct fdeck {
    int loaded, playing, grid_ok;
    double bpm, ph;
};
static struct fdeck g_fd[4];

static int f_deck_info(int deck, az_deck_info *o)
{
    int size = o->size;
    memset(o, 0, sizeof *o);
    o->size = size;
    if (deck < 0 || deck > 3) return -1;
    o->loaded = g_fd[deck].loaded;
    o->playing = g_fd[deck].playing;
    o->pad_mode = -1;
    o->bpm = g_fd[deck].bpm;
    o->len_s = -1;
    return 0;
}
static int f_deck_grid(int deck, double *beat_phase, double *bar_phase)
{
    if (deck < 0 || deck > 3 || !g_fd[deck].grid_ok) return -1;
    *beat_phase = g_fd[deck].ph;
    *bar_phase = -1;
    return 0;
}
UN static void deck_set(int deck, int loaded, int playing, double bpm, double ph, int grid_ok)
{
    g_fd[deck].loaded = loaded;
    g_fd[deck].playing = playing;
    g_fd[deck].bpm = bpm;
    g_fd[deck].ph = ph;
    g_fd[deck].grid_ok = grid_ok;
}
/* decks 1 and 2 loaded and playing at 125 BPM with these beat phases */
UN static void both_play(double ph1, double ph2)
{
    deck_set(0, 1, 1, 125.0, ph1, 1);
    deck_set(1, 1, 1, 125.0, ph2, 1);
}
#define FAKE_MS_PER_BEAT 480.0 /* 125 BPM: 1 ms = 1/480 beat */
/* both decks at 125 BPM, deck 1 ahead of deck 2 by ms (deck 2 at phase 0.30) */
UN static void apart_ms(double ms) { both_play(0.30 + ms / FAKE_MS_PER_BEAT, 0.30); }

/* ---- rows ---- */
#define PM_HANDLE 100
static az_row_v1 g_rows[8];
static int g_nrows, g_rowv[8];
static int g_pm_value;      /* the official phase-meter row's value */
static int g_lookup_off;    /* 1: row_lookup finds no row (-1) */
static int f_register_row(const char *id, const az_row_v1 *row)
{
    (void)id;
    if (g_nrows >= 8) return -1;
    g_rows[g_nrows] = *row;
    g_rowv[g_nrows] = row->kind == AZ_ROW_CHOICE ? row->default_choice : (row->flags & AZ_ROW_DEFAULT_ON) != 0;
    return g_nrows++;
}
static int f_row_value(int h)
{
    if (h == PM_HANDLE) return g_pm_value;
    return h >= 0 && h < g_nrows ? g_rowv[h] : -1;
}
static int f_row_lookup(const char *key)
{
    if (!g_lookup_off && strcmp(key, "phase-meter") == 0) return PM_HANDLE;
    return -1;
}
UN static int row_by_key(const char *key)
{
    for (int i = 0; i < g_nrows; i++)
        if (strcmp(g_rows[i].key, key) == 0) return i;
    return -1;
}

/* ---- the surface ---- */
struct az_surface { int i; };
struct az_draw { int unused; };
static struct az_surface g_surfs[2];
static az_surface_v1 g_sdesc[2];
static az_rect g_splace[2];
static int g_nsurf, g_sshown[2], g_sshow_calls[2], g_sredraws[2], g_splace_calls[2];
static az_surface *f_surface_create(const char *id, const az_surface_v1 *s)
{
    (void)id;
    if (g_nsurf >= 2) return NULL;
    g_sdesc[g_nsurf] = *s;
    g_surfs[g_nsurf].i = g_nsurf;
    return &g_surfs[g_nsurf++];
}
static void f_surface_place(az_surface *s, az_rect r)
{
    g_splace[s->i] = r;
    g_splace_calls[s->i]++;
}
static void f_surface_show(az_surface *s, int on)
{
    g_sshown[s->i] = on;
    g_sshow_calls[s->i]++;
}
static void f_surface_redraw(az_surface *s) { g_sredraws[s->i]++; }

/* ---- the view, the theme, the page ---- */
static int g_view = AZ_VIEW_DECK, g_decks = 2, g_theme;
static int f_view(void) { return g_view; }
static int f_deck_count(void) { return g_decks; }
static int f_ui_theme(void) { return g_theme; }
static atomic_int g_redraw_req; /* request_redraw calls */
static void f_request_redraw(void) { atomic_fetch_add(&g_redraw_req, 1); }

static az_screen_v1 g_page;
static int g_page_set;
static int f_register_screen(const char *id, const az_screen_v1 *s)
{
    (void)id;
    g_page = *s;
    g_page_set = 1;
    return 0;
}

/* ---- the draw helpers record what they are given ---- */
struct fillrec { int x, y, w, h; uint32_t c; };
struct textrec { char s[160]; uint32_t rgb; int x, y, px; };
#define REC_MAX 64
static struct fillrec g_fill[REC_MAX];
static struct textrec g_text[REC_MAX];
static int g_nfill, g_ntext;
static void f_fill(az_draw *d, int x, int y, int w, int h, uint32_t c)
{
    (void)d;
    if (g_nfill < REC_MAX) g_fill[g_nfill] = (struct fillrec){ x, y, w, h, c };
    g_nfill++;
}
static void f_text(az_draw *d, int x, int y, const char *s, int px, uint32_t c)
{
    (void)d;
    if (g_ntext < REC_MAX) {
        snprintf(g_text[g_ntext].s, sizeof g_text[0].s, "%s", s);
        g_text[g_ntext].rgb = c;
        g_text[g_ntext].x = x;
        g_text[g_ntext].y = y;
        g_text[g_ntext].px = px;
    }
    g_ntext++;
}
static void f_header(az_draw *d, const char *t) { f_text(d, 0, 0, t, 20, 0); }
static int f_text_width(const char *s, int px) { return (int)strlen(s) * px * 3 / 5; }
static uint32_t f_color(int role) { return role == AZ_COL_BG ? 0 : 0xffffff; }
static int f_row_height(az_draw *d) { (void)d; return 50; }
UN static void draw_reset(void) { g_nfill = g_ntext = 0; }
UN static int text_eq(const char *s)
{
    for (int i = 0; i < g_ntext && i < REC_MAX; i++)
        if (strcmp(g_text[i].s, s) == 0) return 1;
    return 0;
}

/* ---- the core ---- */
enum { FK_NO_LOOKUP = 1, FK_NO_ROWS = 2, FK_NO_DECK_GRID = 4 };

/* every service this mod can use; the flags take some away */
UN static void fake_core(az_core_v1 *c, int flags)
{
    memset(c, 0, sizeof *c);
    c->abi = AZ_MOD_ABI;
    c->size = sizeof *c;
    c->log = f_log;
    c->register_screen = f_register_screen;
    c->draw_header = f_header;
    c->draw_text = f_text;
    c->fill_rect = f_fill;
    c->color = f_color;
    c->row_height = f_row_height;
    c->text_width = f_text_width;
    c->request_redraw = f_request_redraw;
    c->deck_count = f_deck_count;
    c->ui_theme = f_ui_theme;
    c->view = f_view;
    c->surface_create = f_surface_create;
    c->surface_place = f_surface_place;
    c->surface_show = f_surface_show;
    c->surface_redraw = f_surface_redraw;
    c->deck_info = f_deck_info;
    if (!(flags & FK_NO_DECK_GRID)) c->deck_grid = f_deck_grid;
    if (!(flags & FK_NO_LOOKUP)) c->row_lookup = f_row_lookup;
    if (!(flags & FK_NO_ROWS)) {
        c->register_row = f_register_row;
        c->row_value = f_row_value;
    }
}

/* ---- the mod and the time ---- */
static const az_mod_v1 *M;
static az_core_v1 g_core;

/* fills az_core_v1 and calls init; returns init's result (0 = loaded) */
UN static int load_mod(int flags)
{
    fake_core(&g_core, flags);
    M = az_mod_v1_entry();
    CHECK(M && M->init && M->tick);
    return M->init(&g_core);
}
/* the clock moves ms, then one UI tick */
UN static void tick_ms(int ms)
{
    atomic_fetch_add(&g_sim_ms, ms);
    M->tick();
}
UN static long sim_now(void) { return atomic_load(&g_sim_ms); }

/* ---- the test driver: one process per test ---- */
typedef void (*test_fn)(void);
struct test_entry {
    const char *name;
    test_fn fn;
};

UN static int run_tests(int argc, char **argv, const struct test_entry *tests, int n, const char *suite)
{
    g_main = pthread_self();
    if (argc > 1) {
        for (int i = 0; i < n; i++)
            if (strcmp(argv[1], tests[i].name) == 0) {
                tests[i].fn();
                CHECK(g_log_bad == 0); /* C->log from the main thread only */
                printf("%s: %s, %d checks%s\n", g_fails ? "FAIL" : "ok", tests[i].name, g_checks,
                       g_fails ? " (see above)" : "");
                fflush(stdout);
                return g_fails ? 1 : 0;
            }
        fprintf(stderr, "no test %s\n", argv[1]);
        return 2;
    }
    int bad = 0;
    fflush(stdout);
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 2;
        }
        if (pid == 0) {
            execl(argv[0], argv[0], tests[i].name, (char *)NULL);
            _exit(127);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("FAIL: %s did not pass (status %d)\n", tests[i].name, st);
            bad++;
        }
    }
    printf("%s: %d tests, %d failed\n", suite, n, bad);
    return bad ? 1 : 0;
}

#endif
