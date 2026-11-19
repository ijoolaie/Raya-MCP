#include "reaper-window-v2.h"
size_t raya_reaper_build_window(size_t total, size_t cursor,
                                size_t indices[RAYA_REAPER_WINDOW_CAPACITY],
                                size_t *next_cursor) {
    size_t count, i, index;
    if (next_cursor == NULL) return 0;
    *next_cursor = 0;
    if (total == 0 || indices == NULL) return 0;
    index = cursor % total;
    count = total < RAYA_REAPER_WINDOW_CAPACITY
          ? total : RAYA_REAPER_WINDOW_CAPACITY;
    for (i = 0; i < count; ++i) {
        indices[i] = index;
        index = (index == total - 1) ? 0 : index + 1;
    }
    *next_cursor = index;
    return count;
}
