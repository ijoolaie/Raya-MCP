#ifndef RAYA_COMPLETION_CLASSIFY_V2_H
#define RAYA_COMPLETION_CLASSIFY_V2_H
#include <windows.h>
typedef enum {
    RAYA_COMPLETION_TERMINAL_SUCCESS = 0,
    RAYA_COMPLETION_INCOMPLETE = 1,
    RAYA_COMPLETION_UNCERTAIN = 2
} raya_completion_class;
raya_completion_class raya_classify_completion(BOOL api_success, DWORD error);
#endif
