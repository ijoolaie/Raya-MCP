#include "pipe-io-owner-v2.h"
#include <stdio.h>

static unsigned tests, failures;
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %u: %s\n", tests, #x); } } while (0)

static void immediate_and_submission_failure(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_caller_complete_immediate(&o));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_TERMINAL);
    CHECK(o.terminal_collections == 1);
    CHECK(raya_pipe_owner_release(&o));
    CHECK(!raya_pipe_owner_caller_complete_immediate(&o));
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_caller_submission_failed(&o));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_TERMINAL);
    CHECK(raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(raya_pipe_owner_release(&o));
    CHECK(!raya_pipe_owner_release(&o));
}

static void caller_uncertain_retry(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_caller_collect(&o, FALSE, TRUE, FALSE, ERROR_INVALID_HANDLE));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_UNCERTAIN);
    CHECK(raya_pipe_owner_caller_retry(&o));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_PENDING);
    CHECK(raya_pipe_owner_caller_collect(&o, TRUE, TRUE, TRUE, ERROR_SUCCESS));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_TERMINAL);
    CHECK(raya_pipe_owner_release(&o));
    CHECK(!raya_pipe_owner_caller_retry(&o));
}

static void normal_collection_incomplete(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_PENDING);
    CHECK(raya_pipe_owner_caller_collect(&o, TRUE, TRUE, FALSE, ERROR_IO_INCOMPLETE));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_PENDING);
    CHECK(raya_pipe_owner_caller_may_access(&o));
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(o.terminal_collections == 0 && o.release_commits == 0);
    CHECK(raya_pipe_owner_handoff(&o));
    CHECK(!raya_pipe_owner_caller_may_access(&o));
    CHECK(!raya_pipe_owner_release(&o));
    CHECK(raya_pipe_owner_reaper_take(&o));
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, TRUE, TRUE, ERROR_SUCCESS));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_TERMINAL);
    CHECK(raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(raya_pipe_owner_release(&o));
    CHECK(o.state == RAYA_PIPE_OWNER_RELEASED && o.release_commits == 1);
    CHECK(!raya_pipe_owner_release(&o));
}

static void cancellation_drain_uncertain(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_caller_collect(&o, TRUE, TRUE, FALSE, ERROR_INVALID_HANDLE));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(raya_pipe_owner_handoff(&o));
    CHECK(!raya_pipe_owner_caller_may_access(&o));
    CHECK(raya_pipe_owner_reaper_take(&o));
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, TRUE, FALSE, ERROR_INVALID_HANDLE));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(!raya_pipe_owner_release(&o));
    CHECK(raya_pipe_owner_reaper_retry(&o));
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, TRUE, FALSE, ERROR_IO_INCOMPLETE));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_PENDING);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, TRUE, FALSE, ERROR_OPERATION_ABORTED));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_TERMINAL);
    CHECK(o.terminal_collections == 1);
    CHECK(raya_pipe_owner_release(&o));
}

static void blocking_reaper_collection_uncertain(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_handoff(&o));
    CHECK(raya_pipe_owner_reaper_take(&o));
    /* Models failed GetOverlappedResult(..., TRUE) in the old per-op thread. */
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, TRUE, FALSE, ERROR_ACCESS_DENIED));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
    CHECK(!raya_pipe_owner_release(&o));
    CHECK(!raya_pipe_owner_caller_may_access(&o));
}

static void reject_bad_context_and_duplicate_transitions(void) {
    raya_pipe_owner o;
    raya_pipe_owner_init(&o);
    CHECK(raya_pipe_owner_caller_collect(&o, FALSE, TRUE, TRUE, ERROR_SUCCESS));
    CHECK(o.state == RAYA_PIPE_OWNER_CALLER_UNCERTAIN);
    CHECK(!raya_pipe_owner_caller_collect(&o, TRUE, TRUE, TRUE, ERROR_SUCCESS));
    CHECK(raya_pipe_owner_handoff(&o));
    CHECK(!raya_pipe_owner_handoff(&o));
    CHECK(raya_pipe_owner_reaper_take(&o));
    CHECK(!raya_pipe_owner_reaper_take(&o));
    CHECK(raya_pipe_owner_reaper_collect(&o, TRUE, FALSE, TRUE, ERROR_SUCCESS));
    CHECK(o.state == RAYA_PIPE_OWNER_REAPER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&o));
}

int main(void) {
    immediate_and_submission_failure();
    caller_uncertain_retry();
    normal_collection_incomplete();
    cancellation_drain_uncertain();
    blocking_reaper_collection_uncertain();
    reject_bad_context_and_duplicate_transitions();
    printf("Pipe I/O ownership gate: %u assertions, %u failures\n", tests, failures);
    return failures ? 1 : 0;
}
