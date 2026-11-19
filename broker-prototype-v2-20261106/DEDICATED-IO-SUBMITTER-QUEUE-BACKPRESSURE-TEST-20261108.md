# Dedicated I/O Submitter — bounded queue backpressure test (2026-11-08)

## Scope

Added a small deterministic queue-policy harness:

- `dedicated-io-submitter-queue-backpressure-test.c`
- `build-dedicated-io-submitter-queue-backpressure-test.cmd`

This test is separate from the multi-producer pipe harness and does not modify production modules.

## Method

The Submitter consumer is held behind a gate while the test fills the bounded ring queue. It tests capacities 1 and 2:

- exactly `capacity` entries are accepted;
- the next enqueue is rejected as backpressure;
- accepted and backpressure counts are exact;
- after the consumer gate opens, all accepted entries are consumed exactly once;
- queue count returns to zero;
- consumer thread exits with code zero.

Each capacity case is repeated 25 times (50 cases total).

## Result

MSVC x64, `/W4 /WX`:

- 825 assertions passed.
- 25/25 capacity-1 cases passed.
- 25/25 capacity-2 cases passed.
- No unexpected queue overrun or lost accepted entry.

## Limits

This is a deterministic queue-capacity policy test, not the final Submitter queue implementation. It does not test concurrent producer contention, event wake/reset correctness, admission racing shutdown, or the real public API's backpressure result. The consumer uses a gate to make saturation deterministic; it does not rely on the queue wake event.

Production remains **NO-GO**.
