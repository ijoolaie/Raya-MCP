#ifndef RAYA_REAPER_WINDOW_V2_H
#define RAYA_REAPER_WINDOW_V2_H
#include <stddef.h>
#define RAYA_REAPER_WINDOW_CAPACITY 63u
/*
 * Build a rotating index window over a stable operation array.
 * The caller must hold the lock that stabilizes the array and must validate
 * every selected operation/event before passing handles to a wait API.
 * Returns the number of indices written. next_cursor is the first index for
 * the next rotation. No allocation and no Win32 calls occur here.
 */
size_t raya_reaper_build_window(size_t total, size_t cursor,
                                size_t indices[RAYA_REAPER_WINDOW_CAPACITY],
                                size_t *next_cursor);
#endif
