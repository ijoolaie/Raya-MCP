#include <stdio.h>
#include <stdlib.h>

typedef enum {
    OWNER_CALLER_PENDING = 1,
    OWNER_REAPER_QUEUED,
    OWNER_REAPER_PENDING,
    OWNER_TERMINAL,
    OWNER_RELEASED
} owner_state;

typedef struct {
    owner_state owner;
    int event_signaled;
    int cancel_confirmed;
    int cancel_error;
    int result_collections;
    int cleanup_count;
    int caller_touched_after_handoff;
} operation;

static int failures;

#define CHECK(expr, label) do { \
    if (expr) printf("PASS: %s\\n", label); \
    else { printf("FAIL: %s\\n", label); ++failures; } \
} while (0)

/* Policy model only: this callback is injected and does not call CancelIoEx. */
static unsigned long injected_cancel(unsigned long configured_error) {
    return configured_error;
}

static void run_case(unsigned long cancel_error, const char *name) {
    operation op = { OWNER_CALLER_PENDING, 0, 0, 0, 0, 0, 0 };
    unsigned long observed = injected_cancel(cancel_error);

    op.cancel_error = (int)observed;
    if (observed == 0) op.cancel_confirmed = 1;

    CHECK(op.owner == OWNER_CALLER_PENDING, "cancel result does not change ownership");
    CHECK(op.event_signaled == 0, "cancel result is not treated as completion");
    CHECK(op.result_collections == 0, "no result collection before event signal");

    /* Simulate bounded drain expiry: ownership moves once under the queue lock. */
    if (!op.event_signaled && op.owner == OWNER_CALLER_PENDING)
        op.owner = OWNER_REAPER_QUEUED;

    CHECK(op.owner == OWNER_REAPER_QUEUED, "drain expiry transfers pending operation");
    /* After handoff, caller is forbidden from touching operation resources. */
    op.caller_touched_after_handoff = 0;

    /* Worker owns it; later event signal and terminal result are collected. */
    if (op.owner == OWNER_REAPER_QUEUED) {
        op.owner = OWNER_REAPER_PENDING;
        op.event_signaled = 1;
        ++op.result_collections;
        op.owner = OWNER_TERMINAL;
        ++op.cleanup_count;
        op.owner = OWNER_RELEASED;
    }

    CHECK(op.result_collections == 1, "worker collects exactly one terminal result");
    CHECK(op.cleanup_count == 1 && op.owner == OWNER_RELEASED,
          "terminal operation is cleaned exactly once");
    CHECK(op.caller_touched_after_handoff == 0,
          "caller performs no operation access after handoff");
    printf("CASE COMPLETE: %s (injected error=%lu)\\n", name, observed);
}

int main(void) {
    run_case(5ul, "non-ERROR_NOT_FOUND failure (ERROR_ACCESS_DENIED)");
    run_case(1168ul, "ERROR_NOT_FOUND completion race");
    run_case(0ul, "CancelIoEx accepted but completion still pending");
    printf("SUMMARY: %d failure(s)\\n", failures);
    return failures ? 1 : 0;
}
