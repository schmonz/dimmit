/* Unit tests for the daemon logic and the ddc.c wrapper.
 *
 * dimmitd.c is #included directly (with DIMMIT_TESTING defined, which drops its
 * main()) so the tests can reach its static helpers and globals. The ddc layer
 * is driven through the in-memory mock backend in ddc_impl_test.c. */
#define DIMMIT_TESTING 1
#include "dimmitd.c"

#include <stdio.h>

extern int ddc_test_authorized; /* from ddc_impl_test.c */

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void reset_state(void) {
    current_brightness = 50;
    max_brightness = 100;
    pending_delta = 0;
}

static void test_clamp_brightness(void) {
    CHECK(clamp_brightness(50, 100) == 50);
    CHECK(clamp_brightness(0, 100) == 0);
    CHECK(clamp_brightness(100, 100) == 100);
    CHECK(clamp_brightness(-5, 100) == 0);    /* floor */
    CHECK(clamp_brightness(150, 100) == 100); /* ceiling */
}

static void test_parse_command(void) {
    CHECK(parse_command("up") == STEP);
    CHECK(parse_command("down") == -STEP);
    CHECK(parse_command("bogus") == 0);
    CHECK(parse_command("") == 0);
}

static void test_adjust_brightness(void) {
    /* Accumulates across calls before the worker applies them. */
    reset_state();
    adjust_brightness(STEP);
    CHECK(pending_delta == STEP);
    adjust_brightness(STEP);
    CHECK(pending_delta == 2 * STEP);

    /* A step that would overshoot the ceiling lands exactly on it. */
    reset_state();
    current_brightness = 98;
    adjust_brightness(STEP);
    CHECK(pending_delta == 2);            /* target 100, not 103 */
    adjust_brightness(STEP);
    CHECK(pending_delta == 2);            /* already pinned at 100 */

    /* Same for the floor. */
    reset_state();
    current_brightness = 3;
    adjust_brightness(-STEP);
    CHECK(pending_delta == -3);           /* target 0, not -2 */
    adjust_brightness(-STEP);
    CHECK(pending_delta == -3);
    reset_state();
}

static void test_debounce(void) {
    struct timeval last = {100, 0};
    struct timeval soon = {100, 100000};  /* +100 ms */
    struct timeval edge = {100, 200000};  /* +200 ms */
    struct timeval late = {100, 250000};  /* +250 ms */

    CHECK(elapsed_ms(last, soon) == 100);
    CHECK(elapsed_ms(last, edge) == 200);
    CHECK(elapsed_ms(last, late) == 250);

    CHECK(debounce_ready(last, soon) == 0);  /* 100 < DEBOUNCE_MS */
    CHECK(debounce_ready(last, edge) == 1);  /* 200 >= DEBOUNCE_MS */
    CHECK(debounce_ready(last, late) == 1);

    /* Spanning a whole-second boundary. */
    struct timeval a = {100, 900000};
    struct timeval b = {101, 100000};        /* +200 ms */
    CHECK(elapsed_ms(a, b) == 200);
    CHECK(debounce_ready(a, b) == 1);
}

static void test_authorization(void) {
    ddc_test_authorized = 1;
    CHECK(ddc_is_authorized(0) == 1);
    ddc_test_authorized = 0;
    CHECK(ddc_is_authorized(0) == 0);
    ddc_test_authorized = 1;
}

static void test_ddc_roundtrip(void) {
    ddc_handle_t *h = ddc_open_display();
    CHECK(h != NULL);
    if (!h) return;

    int cur = -1, max = -1;
    CHECK(ddc_get_brightness(h, &cur, &max) == 0);
    CHECK(cur == 50);
    CHECK(max == 100);

    CHECK(ddc_set_brightness(h, 73) == 0);
    CHECK(ddc_get_brightness(h, &cur, &max) == 0);
    CHECK(cur == 73);

    ddc_close_display(h);
}

int main(void) {
    test_clamp_brightness();
    test_parse_command();
    test_adjust_brightness();
    test_debounce();
    test_authorization();
    test_ddc_roundtrip();

    if (failures) {
        fprintf(stderr, "%d/%d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("All %d checks passed\n", checks);
    return 0;
}
