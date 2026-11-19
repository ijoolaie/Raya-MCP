#define WIN32_LEAN_AND_MEAN
#include "completion-classify-v2.h"

/* A failed GetOverlappedResult is not cleanup proof unless its contract
   explicitly establishes incompletion. Unknown failures remain uncertain. */
raya_completion_class raya_classify_completion(BOOL api_success, DWORD error) {
    if (api_success) return RAYA_COMPLETION_TERMINAL_SUCCESS;
    if (error == ERROR_IO_INCOMPLETE) return RAYA_COMPLETION_INCOMPLETE;
    return RAYA_COMPLETION_UNCERTAIN;
}
