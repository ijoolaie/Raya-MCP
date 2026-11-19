#include "reaper-registry-v2.h"
#include <string.h>
static int valid_handle(HANDLE h) {
    return h != NULL && h != INVALID_HANDLE_VALUE;
}
int raya_reaper_registry_add(raya_reaper_registry *r,
                             raya_reaper_op *op, HANDLE event_handle) {
    size_t i;
    if (r == NULL || op == NULL || !valid_handle(event_handle) ||
        r->count > RAYA_REAPER_REGISTRY_CAPACITY) return 0;
    if (r->count == RAYA_REAPER_REGISTRY_CAPACITY) return 0;
    for (i = 0; i < r->count; ++i) {
        if (r->entries[i].op == op ||
            r->entries[i].event_handle == event_handle) return 0;
    }
    r->entries[r->count].op = op;
    r->entries[r->count].event_handle = event_handle;
    ++r->count;
    return 1;
}
int raya_reaper_registry_remove(raya_reaper_registry *r,
                                raya_reaper_op *op) {
    size_t i;
    if (r == NULL || op == NULL || r->count > RAYA_REAPER_REGISTRY_CAPACITY)
        return 0;
    for (i = 0; i < r->count; ++i) {
        if (r->entries[i].op == op) {
            size_t j;
            for (j = i + 1; j < r->count; ++j)
                r->entries[j - 1] = r->entries[j];
            --r->count;
            r->entries[r->count].op = NULL;
            r->entries[r->count].event_handle = NULL;
            if (r->count == 0) r->cursor = 0;
            else if (r->cursor >= r->count) r->cursor %= r->count;
            return 1;
        }
    }
    return 0;
}
int raya_reaper_registry_snapshot(const raya_reaper_registry *r,
                                  raya_reaper_op **ops, HANDLE *events,
                                  size_t capacity, size_t *count,
                                  size_t *cursor) {
    size_t i;
    if (r == NULL || count == NULL || cursor == NULL ||
        r->count > RAYA_REAPER_REGISTRY_CAPACITY ||
        capacity < r->count ||
        (r->count != 0 && (ops == NULL || events == NULL))) return 0;
    for (i = 0; i < r->count; ++i) {
        if (r->entries[i].op == NULL ||
            !valid_handle(r->entries[i].event_handle)) return 0;
        if (ops != NULL) ops[i] = r->entries[i].op;
        if (events != NULL) events[i] = r->entries[i].event_handle;
    }
    *count = r->count;
    *cursor = r->count == 0 ? 0 : r->cursor % r->count;
    return 1;
}
