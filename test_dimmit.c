/* Unit tests for the daemon's logic, exercised through the modules it now links
 * normally: the pure brightness/debounce state machine (dimmer.{c,h}), the
 * command parser (command.{c,h}), and the ddc.c wrapper driven by the in-memory
 * mock backend (ddc_impl_test.c). No #include of dimmitd.c, and no real clock,
 * sockets, or worker thread are involved. */
#include "dimmer.h"
#include "command.h"
#include "ddc.h"

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

static void test_parse_command(void) {
    CHECK(parse_command("up") == STEP);
    CHECK(parse_command("down") == -STEP);
    CHECK(parse_command("bogus") == 0);
    CHECK(parse_command("") == 0);
}

static void test_dimmer_accumulates(void) {
    dimmer_t d;
    struct timeval t = {100, 0};

    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP, t);
    CHECK(d.pending_delta == STEP);
    dimmer_adjust(&d, STEP, t);
    CHECK(d.pending_delta == 2 * STEP);
}

static void test_dimmer_clamps(void) {
    dimmer_t d;
    struct timeval t = {100, 0};

    /* A step that would overshoot the ceiling lands exactly on it. */
    dimmer_init(&d, 98, 100);
    dimmer_adjust(&d, STEP, t);
    CHECK(d.pending_delta == 2);   /* target 100, not 103 */
    dimmer_adjust(&d, STEP, t);
    CHECK(d.pending_delta == 2);   /* already pinned at 100 */

    /* Same for the floor. */
    dimmer_init(&d, 3, 100);
    dimmer_adjust(&d, -STEP, t);
    CHECK(d.pending_delta == -3);  /* target 0, not -2 */
    dimmer_adjust(&d, -STEP, t);
    CHECK(d.pending_delta == -3);
}

static void test_dimmer_due_debounce(void) {
    dimmer_t d;
    struct timeval t0 = {100, 0};
    int target = -1;

    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP, t0);

    struct timeval soon = {100, 100000};  /* +100 ms: not yet */
    CHECK(dimmer_due(&d, soon, &target) == 0);

    struct timeval edge = {100, 200000};  /* +200 ms: due */
    CHECK(dimmer_due(&d, edge, &target) == 1);
    CHECK(target == 55);

    /* Spanning a whole-second boundary still measures 200 ms. */
    dimmer_init(&d, 50, 100);
    struct timeval a = {100, 900000};
    dimmer_adjust(&d, STEP, a);
    struct timeval b = {101, 100000};
    CHECK(dimmer_due(&d, b, &target) == 1);
}

static void test_dimmer_not_due_without_pending(void) {
    dimmer_t d;
    struct timeval late = {200, 0};
    int target = -1;

    dimmer_init(&d, 50, 100);   /* nothing pending */
    CHECK(dimmer_due(&d, late, &target) == 0);
}

static void test_dimmer_commit_and_settled(void) {
    dimmer_t d;
    struct timeval t0 = {100, 0};
    struct timeval edge = {100, 200000};
    int target = -1;

    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP, t0);
    CHECK(dimmer_due(&d, edge, &target) == 1);

    dimmer_commit(&d, target);
    CHECK(d.current == 55);
    dimmer_settled(&d);
    CHECK(d.pending_delta == 0);

    /* Consumed: no longer due. */
    CHECK(dimmer_due(&d, edge, &target) == 0);
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
    test_parse_command();
    test_dimmer_accumulates();
    test_dimmer_clamps();
    test_dimmer_due_debounce();
    test_dimmer_not_due_without_pending();
    test_dimmer_commit_and_settled();
    test_authorization();
    test_ddc_roundtrip();

    if (failures) {
        fprintf(stderr, "%d/%d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("All %d checks passed\n", checks);
    return 0;
}
