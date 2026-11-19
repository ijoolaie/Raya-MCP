#include "pipe-completion-context-v2.h"
#include <stdio.h>

static int failures;
#define CHECK(expr, label) do { \
    if (expr) printf("PASS: %s\n", label); \
    else { printf("FAIL: %s\n", label); ++failures; } \
} while (0)

int main(void) {
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, TRUE, ERROR_SUCCESS) ==
          RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS,
          "signaled exact operation with successful collection is terminal");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_OPERATION_ABORTED) ==
          RAYA_PIPE_COMPLETION_TERMINAL_ABORTED,
          "signaled exact operation collected as operation-aborted is terminal");
    CHECK(raya_classify_pipe_completion(FALSE, TRUE, FALSE, ERROR_OPERATION_ABORTED) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "operation-aborted without its event signal is not cleanup proof");
    CHECK(raya_classify_pipe_completion(TRUE, FALSE, FALSE, ERROR_OPERATION_ABORTED) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "operation-aborted with a non-exact issuing pair is uncertain");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_IO_INCOMPLETE) ==
          RAYA_PIPE_COMPLETION_INCOMPLETE,
          "incomplete collection retains operation ownership");
    CHECK(raya_classify_pipe_completion(FALSE, TRUE, TRUE, ERROR_SUCCESS) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "success without the expected event observation is rejected");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_BROKEN_PIPE) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "disconnect error remains uncertain without its own contract");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_INVALID_HANDLE) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "invalid handle remains uncertain even after event signal");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_ACCESS_DENIED) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "access denied remains uncertain");
    CHECK(raya_classify_pipe_completion(TRUE, TRUE, FALSE, ERROR_INVALID_PARAMETER) ==
          RAYA_PIPE_COMPLETION_UNCERTAIN,
          "invalid parameter remains uncertain");
    printf("SUMMARY: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
