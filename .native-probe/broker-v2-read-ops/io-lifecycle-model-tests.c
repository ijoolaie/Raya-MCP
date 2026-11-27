#include "io-lifecycle-model.h"

#include <stdio.h>

static unsigned g_passed;
static unsigned g_failed;

#define CHECK(name, expression) do { \
    if (expression) { \
        printf("PASS %s\n", name); \
        g_passed++; \
    } else { \
        printf("FAIL %s\n", name); \
        g_failed++; \
    } \
} while (0)

static void test_synchronous_success(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    CHECK("sync success completion observed",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 17));
    CHECK("sync success reaped once", raya_io_reap(&op));
    CHECK("sync success resources released once",
        op.state == RAYA_IO_REAPED && !op.resources_owned &&
        op.cleanup_count == 1 && op.transferred == 17);
}

static void test_synchronous_issue_failure(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    CHECK("sync issue failure is terminal",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_FAILED, 0));
    CHECK("sync issue failure reaped", raya_io_reap(&op));
}

static void test_normal_completion(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    CHECK("pending transition", raya_io_mark_pending(&op));
    CHECK("normal completion before deadline",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 64));
    CHECK("normal completion reaped", raya_io_reap(&op));
}

static void test_cancel_completion(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    CHECK("cancel request transition", raya_io_request_cancel(&op));
    CHECK("cancel completion observed",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_CANCELLED, 0));
    CHECK("cancel completion reaped", raya_io_reap(&op));
}

static void test_cancel_races_normal_completion(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_request_cancel(&op);
    CHECK("cancel race may complete normally",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 8));
    CHECK("cancel race resources released after completion",
        raya_io_reap(&op) && op.cleanup_count == 1);
}

static void test_not_found_requires_completion(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_request_cancel(&op);
    CHECK("cancel not-found race does not permit early reap",
        !raya_io_reap(&op) && op.resources_owned &&
        op.state == RAYA_IO_CANCEL_REQUESTED);
    CHECK("completion independently observed after cancel race",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 3));
    CHECK("cancel not-found race reaped after completion", raya_io_reap(&op));
}

static void test_shutdown_multiple_operations(void)
{
    raya_io_registry registry;
    raya_io_operation a, b;
    raya_io_registry_init(&registry, 2);
    CHECK("admit first operation", raya_io_registry_admit(&registry, &a));
    CHECK("admit second operation", raya_io_registry_admit(&registry, &b));
    raya_io_registry_stop_admission(&registry);
    raya_io_request_cancel(&a);
    raya_io_request_cancel(&b);
    raya_io_observe_completion(&a, RAYA_IO_RESULT_CANCELLED, 0);
    raya_io_observe_completion(&b, RAYA_IO_RESULT_FAILED, 0);
    CHECK("shutdown reaps first exactly once", raya_io_registry_reap(&registry, &a));
    CHECK("shutdown reaps second exactly once", raya_io_registry_reap(&registry, &b));
    CHECK("shutdown registry drains", registry.active_count == 0);
}

static void test_deadline_retains_unresolved_operation(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_request_cancel(&op);
    /* Simulate shutdown grace expiry: no completion is delivered. */
    CHECK("deadline expiry retains unresolved resources",
        op.state == RAYA_IO_CANCEL_REQUESTED && op.resources_owned &&
        !op.completion_observed && op.cleanup_count == 0);
    CHECK("deadline expiry cannot reap unresolved operation", !raya_io_reap(&op));
}

static void test_admission_cap(void)
{
    raya_io_registry registry;
    raya_io_operation a, b, c;
    raya_io_registry_init(&registry, 2);
    raya_io_registry_admit(&registry, &a);
    raya_io_registry_admit(&registry, &b);
    CHECK("hard active-operation cap enforced",
        !raya_io_registry_admit(&registry, &c) && registry.active_count == 2);
}

static void test_duplicate_completion(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 1);
    CHECK("duplicate completion rejected",
        !raya_io_observe_completion(&op, RAYA_IO_RESULT_SUCCESS, 1));
    CHECK("single completion still reaps once",
        raya_io_reap(&op) && op.cleanup_count == 1);
}

static void test_late_completion_after_caller_return(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_request_cancel(&op);
    /* The request owner has returned; the reaper still owns the record. */
    CHECK("late completion has live operation resources",
        op.resources_owned && !op.completion_observed);
    CHECK("late completion accepted by reaper",
        raya_io_observe_completion(&op, RAYA_IO_RESULT_CANCELLED, 0));
    CHECK("late completion frees resources once",
        raya_io_reap(&op) && op.cleanup_count == 1);
}

static void test_failure_completion_and_duplicate_reap(void)
{
    raya_io_operation op;
    raya_io_operation_init(&op);
    raya_io_mark_pending(&op);
    raya_io_observe_completion(&op, RAYA_IO_RESULT_FAILED, 0);
    CHECK("non-cancel failure reaped", raya_io_reap(&op));
    CHECK("duplicate reap rejected",
        !raya_io_reap(&op) && op.cleanup_count == 1);
}

static void test_stop_admission(void)
{
    raya_io_registry registry;
    raya_io_operation op;
    raya_io_registry_init(&registry, 4);
    raya_io_registry_stop_admission(&registry);
    CHECK("shutdown stops new admission",
        !raya_io_registry_admit(&registry, &op) && registry.active_count == 0);
}

static void test_registry_rejects_unregistered_reap(void)
{
    raya_io_registry registry;
    raya_io_operation admitted, foreign;
    raya_io_registry_init(&registry, 2);
    raya_io_registry_admit(&registry, &admitted);
    raya_io_operation_init(&foreign);
    raya_io_observe_completion(&foreign, RAYA_IO_RESULT_SUCCESS, 0);
    CHECK("registry rejects reaping an unregistered operation",
        !raya_io_registry_reap(&registry, &foreign) &&
        registry.active_count == 1 && foreign.resources_owned);
    raya_io_mark_pending(&admitted);
    raya_io_observe_completion(&admitted, RAYA_IO_RESULT_SUCCESS, 0);
    CHECK("registered operation drains registry",
        raya_io_registry_reap(&registry, &admitted) &&
        registry.active_count == 0 && !admitted.registered);
}
int main(void)
{
    test_synchronous_success();
    test_synchronous_issue_failure();
    test_normal_completion();
    test_cancel_completion();
    test_cancel_races_normal_completion();
    test_not_found_requires_completion();
    test_shutdown_multiple_operations();
    test_deadline_retains_unresolved_operation();
    test_admission_cap();
    test_duplicate_completion();
    test_late_completion_after_caller_return();
    test_failure_completion_and_duplicate_reap();
    test_stop_admission();
    test_registry_rejects_unregistered_reap();

    printf("IO_LIFECYCLE_MODEL: %u passed, %u failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
