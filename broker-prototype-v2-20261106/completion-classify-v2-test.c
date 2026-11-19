#include "completion-classify-v2.h"
#include <stdio.h>
static int failures;
#define CHECK(x, label) do { if (x) printf("PASS: %s\n", label); else { printf("FAIL: %s\n", label); ++failures; } } while (0)
int main(void) {
    CHECK(raya_classify_completion(TRUE, ERROR_SUCCESS) == RAYA_COMPLETION_TERMINAL_SUCCESS,
          "successful GetOverlappedResult is terminal");
    CHECK(raya_classify_completion(FALSE, ERROR_IO_INCOMPLETE) == RAYA_COMPLETION_INCOMPLETE,
          "ERROR_IO_INCOMPLETE means still pending");
    CHECK(raya_classify_completion(FALSE, ERROR_OPERATION_ABORTED) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_OPERATION_ABORTED alone is not treated as cleanup proof");
    CHECK(raya_classify_completion(FALSE, ERROR_BROKEN_PIPE) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_BROKEN_PIPE alone is not treated as cleanup proof");
    CHECK(raya_classify_completion(FALSE, ERROR_PIPE_NOT_CONNECTED) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_PIPE_NOT_CONNECTED alone is not treated as cleanup proof");
    CHECK(raya_classify_completion(FALSE, ERROR_NO_DATA) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_NO_DATA alone is not treated as cleanup proof");
    CHECK(raya_classify_completion(FALSE, ERROR_HANDLE_EOF) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_HANDLE_EOF alone is not treated as cleanup proof");
    CHECK(raya_classify_completion(FALSE, ERROR_INVALID_HANDLE) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_INVALID_HANDLE remains uncertain");
    CHECK(raya_classify_completion(FALSE, ERROR_ACCESS_DENIED) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_ACCESS_DENIED remains uncertain");
    CHECK(raya_classify_completion(FALSE, ERROR_INVALID_PARAMETER) == RAYA_COMPLETION_UNCERTAIN,
          "ERROR_INVALID_PARAMETER remains uncertain");
    printf("SUMMARY: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
