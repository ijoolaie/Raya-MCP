#ifndef RAYA_REAPER_REGISTRY_V2_H
#define RAYA_REAPER_REGISTRY_V2_H
#include <windows.h>
#include <stddef.h>
#include "reaper-core-v2.h"
#define RAYA_REAPER_REGISTRY_CAPACITY 256
typedef struct {
    raya_reaper_op *op;
    HANDLE event_handle;
} raya_reaper_registry_entry;
typedef struct {
    raya_reaper_registry_entry entries[RAYA_REAPER_REGISTRY_CAPACITY];
    size_t count;
    size_t cursor;
} raya_reaper_registry;
/* All calls require the same external lock used for operation ownership. */
int raya_reaper_registry_add(raya_reaper_registry *registry,
                             raya_reaper_op *op, HANDLE event_handle);
int raya_reaper_registry_remove(raya_reaper_registry *registry,
                                raya_reaper_op *op);
int raya_reaper_registry_snapshot(const raya_reaper_registry *registry,
                                  raya_reaper_op **ops, HANDLE *events,
                                  size_t capacity, size_t *count,
                                  size_t *cursor);
#endif
