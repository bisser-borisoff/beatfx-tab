/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Host test for nudge-indicator.c: a fake core (mod_fake.h) with fake decks that have a beat phase, a
 * simulated UI clock and one surface. Each test runs in a process of its own (the mod keeps state
 * in statics). Built with ASan + UBSan (make unit) and with TSan (make unit-tsan, where one test
 * paints from a second thread while ticks run).
 *
 * Every test ticks in 40 ms steps (even while the fake view is the menu): the state machine
 * starts afresh after a tick gap of more than TICK_GAP_MS.
 */
#include <pthread.h>

#include "mod_fake.h"
#include "tunables.h"

#define TEST(name) static void test_##name(void)
#define STEP_MS 40

static void sign_of(sign_state *s) { pl_test_sign(s); }

TEST(shows_and_places_meter_off)
{
    CHECK(load_mod(0) == 0);
    /* what init made */
    CHECK(M->abi == AZ_MOD_ABI && M->fw_tags && strcmp(M->fw_tags[0], "any") == 0 && !M->fw_tags[1]);
    CHECK(M->on_frame == NULL && M->tick != NULL);
    CHECK(g_nsurf == 1);
    CHECK(g_sdesc[0].layer == AZ_LAYER_OVER_EP147 && g_sdesc[0].views == AZ_VIEW_BIT(AZ_VIEW_DECK));
    CHECK(g_sdesc[0].on_input == NULL && g_sdesc[0].on_paint != NULL);
    CHECK(!g_sshown[0] && g_sshow_calls[0] == 1 && g_sredraws[0] == 0); /* created hidden */
    CHECK(g_nrows == 2);
    int en = row_by_key("enabled"), st = row_by_key("status");
    CHECK(en == 0 && st == 1);
    if (en >= 0 && st >= 0) {
        CHECK(g_rows[en].kind == AZ_ROW_TOGGLE && (g_rows[en].flags & AZ_ROW_DEFAULT_ON));
        CHECK(strcmp(g_rows[en].label, "Nudge Indicator") == 0);
        CHECK(strcmp(g_rows[en].help,
                     "Shows how far apart the two decks' beats are, and a green line when they are in phase.") == 0);
        CHECK(g_rows[st].kind == AZ_ROW_SCREEN && strcmp(g_rows[st].label, "Status") == 0);
        CHECK(g_rows[st].screen && g_rows[st].screen->on_draw);
    }

    both_play(0.30, 0.325); /* delta -12 ms: deck 2 is ahead, its tick sits left */
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    CHECK(g_splace[0].x == 456 && g_splace[0].y == 259 && g_splace[0].w == 89 && g_splace[0].h == 27);
    sign_state s;
    sign_of(&s);
    CHECK(s.locked == 0 && s.dx == -12 && s.ms == 12);
    pl_out o;
    CHECK(pl_test_out(&o) == 1);
    CHECK(o.state == PL_NUDGE && o.why == PL_APART);
    CHECK(g_sredraws[0] == 1);
}

TEST(places_meter_on)
{
    g_pm_value = 1;
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    CHECK(g_splace[0].x == 42 && g_splace[0].y == 90 && g_splace[0].w == 89 && g_splace[0].h == 27);
}

TEST(no_row_lookup_means_off)
{
    g_pm_value = 1;
    CHECK(load_mod(FK_NO_LOOKUP) == 0); /* a core without row_lookup */
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    CHECK(g_splace[0].x == 456 && g_splace[0].y == 259 && g_splace[0].w == 89 && g_splace[0].h == 27);
}

TEST(row_lookup_minus_one_means_off)
{
    g_pm_value = 1;
    g_lookup_off = 1; /* row_lookup answers -1 */
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    CHECK(g_splace[0].x == 456 && g_splace[0].y == 259 && g_splace[0].w == 89 && g_splace[0].h == 27);
}

TEST(no_rows_service_still_runs)
{
    CHECK(load_mod(FK_NO_ROWS) == 0); /* no register_row: always on, the page through register_screen */
    CHECK(g_nrows == 0 && g_page_set && g_page.on_draw != NULL);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    CHECK(g_splace[0].x == 456 && g_splace[0].y == 259);
}

TEST(mode_switch_while_shown)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1 && g_splace[0].x == 456 && g_splace[0].y == 259);
    int before = g_sredraws[0];
    g_pm_value = 1;
    tick_ms(STEP_MS);
    CHECK(g_splace[0].x == 42 && g_splace[0].y == 90 && g_splace[0].w == 89 && g_splace[0].h == 27);
    CHECK(g_sshown[0] == 1);
    CHECK(g_sredraws[0] == before + 1);
    /* and back */
    g_pm_value = 0;
    tick_ms(STEP_MS);
    CHECK(g_splace[0].x == 456 && g_splace[0].y == 259);
    CHECK(g_sshown[0] == 1 && g_sredraws[0] == before + 2);
}

static void hidden_case(const char *what, void (*brk)(void), void (*fix)(void))
{
    brk();
    tick_ms(STEP_MS);
    if (g_sshown[0]) printf("not hidden: %s\n", what);
    CHECK(g_sshown[0] == 0);
    fix();
    tick_ms(STEP_MS);
    if (!g_sshown[0]) printf("not shown again: %s\n", what);
    CHECK(g_sshown[0] == 1);
}
static void brk_view(void) { g_view = AZ_VIEW_MENU; }
static void fix_view(void) { g_view = AZ_VIEW_DECK; }
static void brk_theme(void) { g_theme = 1; }
static void fix_theme(void) { g_theme = 0; }
static void brk_decks(void) { g_decks = 4; }
static void fix_decks(void) { g_decks = 2; }
static void brk_deck3(void) { g_fd[2].loaded = 1; }
static void fix_deck3(void) { g_fd[2].loaded = 0; }
static void brk_row(void) { g_rowv[row_by_key("enabled")] = 0; }
static void fix_row(void) { g_rowv[row_by_key("enabled")] = 1; }
static void brk_stop2(void) { g_fd[1].playing = 0; }
static void fix_stop2(void) { g_fd[1].playing = 1; }

TEST(hidden_cases)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1);
    hidden_case("view is the menu", brk_view, fix_view);
    hidden_case("theme on", brk_theme, fix_theme);
    hidden_case("4 decks", brk_decks, fix_decks);
    hidden_case("deck 3 loaded", brk_deck3, fix_deck3);
    hidden_case("row off", brk_row, fix_row);
    hidden_case("deck 2 stopped", brk_stop2, fix_stop2);
}

TEST(lock_shows_line_and_stops_redrawing)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.30);
    tick_ms(0); /* t0 */
    sign_state s;
    int elapsed = 0, locked_at = -1;
    while (elapsed < 400 && locked_at < 0) {
        tick_ms(STEP_MS);
        elapsed += STEP_MS;
        sign_of(&s);
        if (s.locked) locked_at = elapsed;
        else CHECK(elapsed < 250 || !s.locked);
    }
    CHECK(locked_at == 280);
    pl_out o;
    CHECK(pl_test_out(&o) == 1 && o.state == PL_LOCKED && o.why == PL_OK);
    CHECK(g_sshown[0] == 1);
    int before = g_sredraws[0];
    for (int i = 0; i < 100; i++) {
        apart_ms((i % 3) * 1.0); /* wobble 0..2 ms */
        tick_ms(STEP_MS);
    }
    sign_of(&s);
    CHECK(s.locked == 1);
    CHECK(g_sredraws[0] == before);
}

TEST(nudge_redraw_cap)
{
    CHECK(load_mod(0) == 0);
    apart_ms(14.0);
    tick_ms(0);
    for (int k = 1; k <= 25; k++) {
        apart_ms(14.0 - 10.0 * k / 25.0); /* 14 -> 4 ms over 1000 ms */
        tick_ms(STEP_MS);
    }
    int n = g_sredraws[0];
    if (n < 6 || n > 9) printf("redraws in the sweep: %d\n", n);
    CHECK(n >= 6 && n <= 9);
    for (int k = 0; k < 4; k++) tick_ms(STEP_MS); /* 160 ms at 4 ms: the picture catches up */
    sign_state s;
    pl_out o;
    sign_of(&s);
    pl_test_out(&o);
    CHECK(o.state == PL_NUDGE && o.dx == 4);
    CHECK(s.locked == 0 && s.dx == o.dx && s.ms == o.ms);
}

TEST(state_change_paints_at_once)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(0);
    CHECK(g_sredraws[0] == 1 && g_sshown[0] == 1);
    g_fd[1].playing = 0; /* nudge -> hidden */
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 0);
    g_fd[1].playing = 1; /* hidden -> nudge, 80 ms after the first paint: painted at once */
    tick_ms(STEP_MS);
    CHECK(g_sshown[0] == 1 && g_sredraws[0] == 2);
    sign_state s;
    sign_of(&s);
    CHECK(s.locked == 0 && s.dx == -12 && s.ms == 12);
    /* nudge -> locked inside the cap: painted at once as well */
    both_play(0.30, 0.30);
    for (int i = 0; i < 20; i++) tick_ms(STEP_MS);
    sign_of(&s);
    CHECK(s.locked == 1);
    int before = g_sredraws[0];
    both_play(0.30, 0.325); /* locked -> nudge (24 ms, over LOCK_OUT_MS) */
    tick_ms(STEP_MS);
    sign_of(&s);
    CHECK(s.locked == 0 && s.dx == -12 && g_sredraws[0] == before + 1);
}

TEST(init_refuses_without_deck_grid)
{
    CHECK(load_mod(FK_NO_DECK_GRID) == 2);
    CHECK(g_nsurf == 0);
    CHECK(g_nlog >= 1);
}

TEST(init_refuses_without_surfaces)
{
    fake_core(&g_core, 0);
    g_core.surface_place = NULL;
    M = az_mod_v1_entry();
    CHECK(M->init(&g_core) == 2);
    CHECK(g_nsurf == 0);
}

TEST(paint_draws_decided_state)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    sign_state s;
    sign_of(&s);
    struct az_draw d = { 0 };

    draw_reset();
    g_sdesc[0].on_paint(&d, g_splace[0], g_sdesc[0].user);
    int nf = g_nfill, nt = g_ntext;
    struct fillrec f[REC_MAX];
    struct textrec t[REC_MAX];
    memcpy(f, g_fill, sizeof f);
    memcpy(t, g_text, sizeof t);
    CHECK(nf == 3 && nt == 2);

    draw_reset();
    sign_paint(&g_core, &d, g_splace[0], &s);
    CHECK(g_nfill == nf && g_ntext == nt);
    CHECK(memcmp(f, g_fill, sizeof(struct fillrec) * (size_t)nf) == 0);
    for (int i = 0; i < nt; i++)
        CHECK(strcmp(t[i].s, g_text[i].s) == 0 && t[i].x == g_text[i].x && t[i].y == g_text[i].y &&
              t[i].px == g_text[i].px && t[i].rgb == g_text[i].rgb);
}

/* the overlay thread paints while the UI thread ticks (TSan: the paint copy is under the mutex) */
static atomic_int g_stop_paint;
static void *paint_thread(void *arg)
{
    (void)arg;
    struct az_draw d = { 0 };
    az_rect area = g_splace[0];
    while (!atomic_load(&g_stop_paint)) {
        draw_reset();
        g_sdesc[0].on_paint(&d, area, g_sdesc[0].user);
    }
    return NULL;
}
TEST(paint_thread_while_ticking)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, paint_thread, NULL) == 0);
    for (int i = 0; i < 400; i++) {
        apart_ms((i / 10) % 2 ? 30.0 - (i % 10) : 2.0 + (i % 10) * 3.0);
        tick_ms(STEP_MS);
    }
    atomic_store(&g_stop_paint, 1);
    pthread_join(th, NULL);
    CHECK(g_sredraws[0] > 1);
}

/* ---- Task 5: the Status page ---- */

static const az_screen_v1 *status_page(void)
{
    int st = row_by_key("status");
    return st >= 0 ? g_rows[st].screen : NULL;
}
/* the page draws once into the area the deck gives it; the texts are in g_text */
static int page_lines(void)
{
    const az_screen_v1 *pg = status_page();
    struct az_draw d = { 0 };
    draw_reset();
    if (pg && pg->on_draw) pg->on_draw(&d, (az_rect){ 0, 100, 600, 395 }, pg->user);
    return g_ntext;
}
static int has(const char *s)
{
    page_lines();
    int ok = text_eq(s);
    if (!ok) {
        printf("missing line: \"%s\"\n  page:", s);
        for (int i = 0; i < g_ntext && i < REC_MAX; i++) printf(" [%s]", g_text[i].s);
        printf("\n");
    }
    return ok;
}
static int lock_is(const char *reason)
{
    char want[200];
    snprintf(want, sizeof want, "In phase: no, %s", reason);
    return has(want);
}

TEST(page_playing_apart)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    tick_ms(STEP_MS);
    CHECK(page_lines() == 6);
    CHECK(strcmp(g_text[0].s, "Nudge Indicator") == 0); /* the header, then one text row each at 20 px */
    CHECK(strcmp(g_text[1].s, "Deck 1: plays, 125.0 BPM, phase 0.30") == 0);
    CHECK(strcmp(g_text[2].s, "Deck 2: plays, 125.0 BPM, phase 0.33") == 0 ||
          strcmp(g_text[2].s, "Deck 2: plays, 125.0 BPM, phase 0.32") == 0);
    CHECK(strcmp(g_text[3].s, "Difference: deck 2 ahead by 12.0 ms") == 0);
    CHECK(strcmp(g_text[4].s, "In phase: no, 12 ms apart") == 0);
    CHECK(strcmp(g_text[5].s, "Place: right of the loop sign (Phase Meter off)") == 0);
    for (int i = 1; i < 6; i++) CHECK(g_text[i].px == 20 && g_text[i].x > 0 && g_text[i].y > g_text[i - 1].y);
}

TEST(page_difference_and_place)
{
    g_pm_value = 1;
    CHECK(load_mod(0) == 0);
    apart_ms(12.0);
    tick_ms(STEP_MS);
    CHECK(has("Difference: deck 1 ahead by 12.0 ms"));
    CHECK(has("Place: left of the Phase Meter (Phase Meter on)"));
    apart_ms(0.2);
    tick_ms(STEP_MS);
    CHECK(has("Difference: in phase (0.2 ms)"));
    apart_ms(-0.2);
    tick_ms(STEP_MS);
    CHECK(has("Difference: in phase (0.2 ms)"));
    apart_ms(0.6);
    tick_ms(STEP_MS);
    CHECK(has("Difference: deck 1 ahead by 0.6 ms"));
    g_fd[1].playing = 0;
    tick_ms(STEP_MS);
    CHECK(has("Difference: -"));
    CHECK(has("Deck 2: stopped"));
}

static void to_hidden(void) /* a tick with deck 2 stopped: the next shows from hidden */
{
    g_fd[1].playing = 0;
    tick_ms(STEP_MS);
    g_fd[1].playing = 1;
}
static void fresh(void)
{
    both_play(0.30, 0.325);
    g_fd[2].loaded = g_fd[3].loaded = 0;
    g_rowv[row_by_key("enabled")] = 1;
    g_theme = 0;
    g_decks = 2;
}

TEST(page_each_reason)
{
    CHECK(load_mod(0) == 0);
    int en = row_by_key("enabled");
    fresh();
    tick_ms(STEP_MS);

    g_rowv[en] = 0; tick_ms(STEP_MS);
    CHECK(lock_is("Nudge Indicator is off")); CHECK(has("Difference: -"));
    fresh(); tick_ms(STEP_MS);
    g_theme = 1; tick_ms(STEP_MS);
    CHECK(lock_is("a theme is on"));
    fresh(); tick_ms(STEP_MS);
    g_decks = 4; tick_ms(STEP_MS);
    CHECK(lock_is("not the 2 DECK view"));
    fresh(); tick_ms(STEP_MS);
    g_fd[3].loaded = 1; tick_ms(STEP_MS);
    CHECK(lock_is("deck 3 or 4 has a track"));
    fresh(); tick_ms(STEP_MS);
    g_fd[2].loaded = 1; tick_ms(STEP_MS);
    CHECK(lock_is("deck 3 or 4 has a track"));
    fresh(); tick_ms(STEP_MS);

    g_fd[0].loaded = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 1 has no track")); CHECK(has("Deck 1: no track"));
    fresh(); tick_ms(STEP_MS);
    g_fd[1].loaded = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 2 has no track")); CHECK(has("Deck 2: no track"));
    fresh(); tick_ms(STEP_MS);
    g_fd[0].playing = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 1 is stopped")); CHECK(has("Deck 1: stopped"));
    fresh(); tick_ms(STEP_MS);
    g_fd[1].playing = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 2 is stopped")); CHECK(has("Deck 2: stopped"));
    fresh(); tick_ms(STEP_MS);
    g_fd[0].grid_ok = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 1 has no beat grid")); CHECK(has("Deck 1: no beat grid"));
    fresh(); g_fd[0].grid_ok = 1; tick_ms(STEP_MS);
    g_fd[1].grid_ok = 0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 2 has no beat grid")); CHECK(has("Deck 2: no beat grid"));
    fresh(); g_fd[1].grid_ok = 1; tick_ms(STEP_MS);
    g_fd[0].bpm = 0.0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 1's tempo is unknown")); CHECK(has("Deck 1: tempo unknown"));
    fresh(); tick_ms(STEP_MS);
    g_fd[1].bpm = -1.0; tick_ms(STEP_MS);
    CHECK(lock_is("deck 2's tempo is unknown")); CHECK(has("Deck 2: tempo unknown"));
    fresh(); tick_ms(STEP_MS);

    to_hidden(); /* from hidden a tempo mismatch hides at once */
    g_fd[0].bpm = 125.0;
    g_fd[1].bpm = 122.0;
    tick_ms(STEP_MS);
    CHECK(lock_is("tempos differ (125.0 / 122.0)"));
    CHECK(has("Deck 2: plays, 122.0 BPM, phase 0.33") || has("Deck 2: plays, 122.0 BPM, phase 0.32"));
    fresh(); tick_ms(STEP_MS);

    both_play(0.30, 0.325); tick_ms(STEP_MS);
    CHECK(lock_is("12 ms apart"));
    both_play(0.30, 0.30); tick_ms(STEP_MS);
    CHECK(lock_is("settling"));
    for (int i = 0; i < 8; i++) tick_ms(STEP_MS);
    CHECK(has("In phase: yes"));
}

TEST(page_view_not_a_reason)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.30);
    g_view = AZ_VIEW_MENU; /* the page is open in the menu */
    tick_ms(0);
    for (int i = 0; i < 8; i++) tick_ms(STEP_MS); /* 320 ms */
    CHECK(has("In phase: yes"));
    CHECK(g_sshown[0] == 0); /* the sign itself stays away from the menu */
    apart_ms(12.0);
    tick_ms(STEP_MS);
    CHECK(has("In phase: no, 12 ms apart"));
}

TEST(page_refresh_rate)
{
    CHECK(load_mod(0) == 0);
    both_play(0.30, 0.325);
    for (int i = 0; i < 10; i++) tick_ms(STEP_MS);
    CHECK(atomic_load(&g_redraw_req) == 0); /* the page was never open: no requests */

    const az_screen_v1 *pg = status_page();
    CHECK(pg && pg->on_enter);
    if (pg && pg->on_enter) pg->on_enter(pg->user);
    int seen = 0;
    for (int i = 0; i < 25; i++) { /* 1000 ms; the core repaints the page on each request */
        tick_ms(STEP_MS);
        if (atomic_load(&g_redraw_req) > seen) {
            seen = atomic_load(&g_redraw_req);
            page_lines();
        }
    }
    CHECK(seen >= 3 && seen <= 5);

    for (int i = 0; i < 18; i++) tick_ms(STEP_MS); /* not drawn: the window of 700 ms runs out */
    int after = atomic_load(&g_redraw_req);
    for (int i = 0; i < 25; i++) tick_ms(STEP_MS);
    CHECK(atomic_load(&g_redraw_req) == after);
}

int main(int argc, char **argv)
{
    static const struct test_entry tests[] = {
#define T(n) { #n, test_##n }
        T(shows_and_places_meter_off),
        T(places_meter_on),
        T(no_row_lookup_means_off),
        T(row_lookup_minus_one_means_off),
        T(no_rows_service_still_runs),
        T(mode_switch_while_shown),
        T(hidden_cases),
        T(lock_shows_line_and_stops_redrawing),
        T(nudge_redraw_cap),
        T(state_change_paints_at_once),
        T(init_refuses_without_deck_grid),
        T(init_refuses_without_surfaces),
        T(paint_draws_decided_state),
        T(paint_thread_while_ticking),
        T(page_playing_apart),
        T(page_difference_and_place),
        T(page_each_reason),
        T(page_view_not_a_reason),
        T(page_refresh_rate),
#undef T
    };
    return run_tests(argc, argv, tests, (int)(sizeof tests / sizeof tests[0]), "test_mod");
}
