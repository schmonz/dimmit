/* Unit tests for the daemon's logic, exercised through the modules it now links
 * normally: the pure brightness state machine (dimmer.{c,h}), the
 * command parser (command.{c,h}), the ddc abstraction driven by the in-memory
 * mock backend (platform/ddc/in_memory_mock.c), and the access-control mock
 * (platform/access-control/mock.c). No #include of dimmitd.c, and no real
 * clock, sockets, or worker thread are involved. */
#include "dimmer.h"
#include "command.h"
#include "brightness.h"
#include "display_controller.h"
#include "platform/ddc/abstraction.h"
#include "platform/ddc/in_memory_mock.h"
#include "platform/access-control/access-control.h"

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#endif

extern int access_control_mock_authorized; /* from platform/access-control/mock.c */

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

    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP);
    CHECK(d.pending_delta == STEP);
    dimmer_adjust(&d, STEP);
    CHECK(d.pending_delta == 2 * STEP);
}

static void test_dimmer_clamps(void) {
    dimmer_t d;

    /* A step that would overshoot the ceiling lands exactly on it. */
    dimmer_init(&d, 98, 100);
    dimmer_adjust(&d, STEP);
    CHECK(d.pending_delta == 2);   /* target 100, not 103 */
    dimmer_adjust(&d, STEP);
    CHECK(d.pending_delta == 2);   /* already pinned at 100 */

    /* Same for the floor. */
    dimmer_init(&d, 3, 100);
    dimmer_adjust(&d, -STEP);
    CHECK(d.pending_delta == -3);  /* target 0, not -2 */
    dimmer_adjust(&d, -STEP);
    CHECK(d.pending_delta == -3);
}

static void test_dimmer_due(void) {
    dimmer_t d;
    int target = -1;

    /* Due as soon as there's a pending delta -- no time gate (leading edge). */
    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP);
    CHECK(dimmer_due(&d, &target) == 1);
    CHECK(target == 55);
}

static void test_dimmer_not_due_without_pending(void) {
    dimmer_t d;
    int target = -1;

    dimmer_init(&d, 50, 100);   /* nothing pending */
    CHECK(dimmer_due(&d, &target) == 0);
}

static void test_dimmer_commit_and_settled(void) {
    dimmer_t d;
    int target = -1;

    dimmer_init(&d, 50, 100);
    dimmer_adjust(&d, STEP);
    CHECK(dimmer_due(&d, &target) == 1);

    dimmer_commit(&d, target);
    CHECK(d.current == 55);
    CHECK(d.pending_delta == 0);   /* commit subtracts the applied step */

    /* Consumed: no longer due. */
    CHECK(dimmer_due(&d, &target) == 0);

    /* settled() abandons a pending batch (used on write failure). */
    dimmer_adjust(&d, -STEP);
    CHECK(d.pending_delta == -STEP);
    dimmer_settled(&d);
    CHECK(d.pending_delta == 0);
    CHECK(dimmer_due(&d, &target) == 0);
}

/* Leading edge: the first press is due immediately, and presses that arrive
 * while a write is in flight (between dimmer_due and dimmer_commit) are not
 * lost -- commit preserves them for the next cycle. */
static void test_dimmer_coalesces_during_write(void) {
    dimmer_t d;
    int target = -1;

    dimmer_init(&d, 100, 100);
    dimmer_adjust(&d, -STEP);                 /* press 1 */
    CHECK(dimmer_due(&d, &target) == 1);
    CHECK(target == 95);                      /* worker captures 95, starts write */

    dimmer_adjust(&d, -STEP);                 /* press 2 lands mid-write */

    dimmer_commit(&d, target);                /* write of 95 finished */
    CHECK(d.current == 95);
    CHECK(d.pending_delta == -STEP);          /* press 2 survived */

    CHECK(dimmer_due(&d, &target) == 1);
    CHECK(target == 90);                      /* next cycle applies press 2 */
    dimmer_commit(&d, target);
    CHECK(d.current == 90);
    CHECK(d.pending_delta == 0);
    CHECK(dimmer_due(&d, &target) == 0);
}

static void test_dimmer_fraction(void) {
    dimmer_t d;
    dimmer_init(&d, 50, 90);
    CHECK(dimmer_max(&d) == 90);

    /* fraction -> delta against a display range; never a no-op step */
    CHECK(dimmer_delta_for_fraction(100,  1.0/16.0) == 6);
    CHECK(dimmer_delta_for_fraction(100, -1.0/16.0) == -6);
    CHECK(dimmer_delta_for_fraction(100,  1.0/64.0) == 2);
    CHECK(dimmer_delta_for_fraction(4,    1.0/64.0) == 1);   /* rounds to 0 -> bumped to 1 */
    CHECK(dimmer_delta_for_fraction(4,   -1.0/64.0) == -1);
}

/* End to end: a client writes a command over a real socket, the daemon's
 * read_command() reads and parses it, and the resulting delta is driven through
 * the dimmer pipeline against the in-memory mock display -- so a request lands
 * as an actual simulated brightness change, with no daemon process, real clock,
 * or worker thread. */
static void test_command_loop_end_to_end(void) {
#ifdef _WIN32
    /* socketpair() is POSIX-only. The socket transport is verified on Windows by
     * the manual daemon+client smoke test (see docs note); the parse->dimmer->
     * mock-display logic this test also covers is exercised by the other tests. */
    fprintf(stderr, "SKIP test_command_loop_end_to_end on Windows (no socketpair)\n");
#else
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    /* Client side writes "down\n"; daemon side reads + parses one command. */
    const char *msg = "down\n";
    CHECK(write(sv[0], msg, strlen(msg)) == (ssize_t)strlen(msg));
    int delta = read_command(sv[1]);
    CHECK(delta == -STEP);

    /* Open the mock display and run the parsed command all the way through the
     * dimmer pipeline as the worker would. */
    ddc_handle_t *h = ddc_open_display();
    CHECK(h != NULL);
    if (h) {
        int cur = -1, max = -1;
        CHECK(ddc_get_brightness(h, &cur, &max) == 0);   /* 50 / 100 */

        dimmer_t d;
        dimmer_init(&d, cur, max);

        dimmer_adjust(&d, delta);

        int target = -1;
        CHECK(dimmer_due(&d, &target) == 1);
        CHECK(target == 45);

        CHECK(ddc_set_brightness(h, target) == 0);
        dimmer_commit(&d, target);

        CHECK(ddc_get_brightness(h, &cur, &max) == 0);
        CHECK(cur == 45);  /* the simulated display actually moved */

        ddc_close_display(h);
    }

    close(sv[0]);
    close(sv[1]);
#endif
}

static void test_authorization(void) {
    access_control_mock_authorized = 1;
    CHECK(access_control_is_authorized(0) == 1);
    access_control_mock_authorized = 0;
    CHECK(access_control_is_authorized(0) == 0);
    access_control_mock_authorized = 1;
}

static void test_brightness_enumerate_multi(void) {
    int currents[] = {50, 20, 80};
    int maxes[]    = {100, 100, 255};
    mock_reset(3, currents, maxes);

    brightness_source *s = NULL; int n = -1;
    CHECK(brightness_enumerate(&s, &n) == 0);
    CHECK(n == 3);

    int cur = -1, max = -1;
    CHECK(s[1].ops->get(s[1].ctx, &cur, &max) == 0);
    CHECK(cur == 20); CHECK(max == 100);

    CHECK(s[2].ops->set(s[2].ctx, 100) == 0);
    CHECK(mock_current(2) == 100);

    /* ids are distinct (needed later for reconcile). */
    CHECK(strcmp(s[0].id, s[1].id) != 0);

    brightness_free(s, n);
    mock_reset(1, (int[]){50}, (int[]){100});  /* restore default for other tests */
}

static void test_controller_lockstep_preserves_offset(void) {
    /* Two displays, different starting levels + different max. */
    mock_reset(2, (int[]){50, 20}, (int[]){100, 100});
    display_controller *c = controller_open();
    CHECK(c != NULL);
    CHECK(controller_count(c) == 2);

    controller_adjust(c, -1.0/16.0);      /* -6 on each (fraction of own max=100) */
    CHECK(controller_service(c) == 2);
    CHECK(controller_current(c, 0) == 44);
    CHECK(controller_current(c, 1) == 14);  /* 30-point offset preserved */

    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}

static void test_controller_clamps_at_rails(void) {
    mock_reset(2, (int[]){3, 98}, (int[]){100, 100});
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0);      /* both want -6 */
    controller_service(c);
    CHECK(controller_current(c, 0) == 0);   /* clamped at floor */
    CHECK(controller_current(c, 1) == 92);
    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}

static void test_controller_partial_failure_isolated(void) {
    mock_reset(2, (int[]){50, 50}, (int[]){100, 100});
    mock_set_fail(0, 1);                    /* display 0 writes fail */
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0);
    CHECK(controller_service(c) == 1);      /* only display 1 written */
    CHECK(mock_current(0) == 50);           /* display 0 unchanged */
    CHECK(mock_current(1) == 44);           /* display 1 moved */
    controller_close(c);
    mock_set_fail(0, 0);
    mock_reset(1, (int[]){50}, (int[]){100});
}

static void test_controller_reconcile_add_and_keep(void) {
    mock_reset(1, (int[]){50}, (int[]){100});
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0); controller_service(c);
    CHECK(controller_current(c, 0) == 44);   /* display 0 moved to 44 */

    /* A second monitor is plugged in. */
    mock_reset(2, (int[]){44, 70}, (int[]){100, 100});
    controller_reconcile(c);
    CHECK(controller_count(c) == 2);
    CHECK(controller_current(c, 0) == 44);   /* kept its level */
    CHECK(controller_current(c, 1) == 70);   /* new one initialized from its current */

    /* Unplug the first. */
    mock_reset(1, (int[]){70}, (int[]){100});
    controller_reconcile(c);
    CHECK(controller_count(c) == 1);
    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
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
    test_dimmer_due();
    test_dimmer_not_due_without_pending();
    test_dimmer_commit_and_settled();
    test_dimmer_coalesces_during_write();
    test_dimmer_fraction();
    test_command_loop_end_to_end();
    test_authorization();
    test_brightness_enumerate_multi();
    test_controller_lockstep_preserves_offset();
    test_controller_clamps_at_rails();
    test_controller_partial_failure_isolated();
    test_controller_reconcile_add_and_keep();
    test_ddc_roundtrip();

    if (failures) {
        fprintf(stderr, "%d/%d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("All %d checks passed\n", checks);
    return 0;
}
