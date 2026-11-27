#include "read-frame.h"

#include <stdio.h>
#include <limits.h>

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s\n", label); ++failed; } \
} while (0)

static void test_deadline_basics(void)
{
    raya_deadline deadline = { 0 };

    CHECK("initialize overall request deadline",
        raya_deadline_init(&deadline, 1000, 5000));
    CHECK("deadline expiry timestamp fixed once",
        deadline.expires_at_ms == 6000);
    CHECK("remaining time at start",
        raya_deadline_remaining_ms(&deadline, 1000) == 5000);
    CHECK("remaining time decreases monotonically",
        raya_deadline_remaining_ms(&deadline, 4500) == 1500);
    CHECK("deadline expires at exact boundary",
        raya_deadline_remaining_ms(&deadline, 6000) == 0);
    CHECK("deadline remains expired after boundary",
        raya_deadline_remaining_ms(&deadline, 9000) == 0);
}

static void test_per_frame_budget(void)
{
    raya_deadline deadline = { 0 };
    raya_deadline_init(&deadline, 1000, 5000);

    CHECK("per-frame budget uses smaller frame timeout",
        raya_deadline_frame_budget_ms(&deadline, 1000, 1000) == 1000);
    CHECK("per-frame budget is capped by remaining overall time",
        raya_deadline_frame_budget_ms(&deadline, 5700, 1000) == 300);
    CHECK("per-frame budget never exceeds overall deadline",
        raya_deadline_frame_budget_ms(&deadline, 5999, 1000) == 1);
    CHECK("per-frame budget is zero after overall deadline",
        raya_deadline_frame_budget_ms(&deadline, 6000, 1000) == 0);
    CHECK("zero per-frame timeout is rejected as zero budget",
        raya_deadline_frame_budget_ms(&deadline, 1000, 0) == 0);
}

static void test_deadline_is_not_reset_per_frame(void)
{
    raya_deadline deadline = { 0 };
    raya_deadline_init(&deadline, 500, 3000);

    CHECK("first frame receives full per-frame cap",
        raya_deadline_frame_budget_ms(&deadline, 500, 800) == 800);
    CHECK("second frame shares same overall deadline",
        raya_deadline_frame_budget_ms(&deadline, 1700, 800) == 800);
    CHECK("late frame receives only remaining overall budget",
        raya_deadline_frame_budget_ms(&deadline, 3400, 800) == 100);
    CHECK("next frame cannot reset expired request deadline",
        raya_deadline_frame_budget_ms(&deadline, 3500, 800) == 0);
}

static void test_invalid_deadlines(void)
{
    raya_deadline deadline = { 0 };

    CHECK("reject zero overall timeout",
        !raya_deadline_init(&deadline, 10, 0));
    CHECK("reject null deadline pointer",
        !raya_deadline_init(NULL, 10, 100));
    CHECK("reject timestamp overflow",
        !raya_deadline_init(&deadline, UINT64_MAX - 2u, 10));
    CHECK("null deadline has no remaining budget",
        raya_deadline_remaining_ms(NULL, 0) == 0);
    CHECK("remaining budget saturates to DWORD range",
        raya_deadline_init(&deadline, 0, UINT32_MAX) &&
        raya_deadline_remaining_ms(&deadline, 0) == UINT32_MAX);
}

int main(void)
{
    test_deadline_basics();
    test_per_frame_budget();
    test_deadline_is_not_reset_per_frame();
    test_invalid_deadlines();

    printf("READ_DEADLINE_TESTS: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
