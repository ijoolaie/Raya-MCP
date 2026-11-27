#ifndef RAYA_BROKER_V2_HANDLE_CHAIN_H
#define RAYA_BROKER_V2_HANDLE_CHAIN_H
#include <windows.h>
#include <stddef.h>

typedef struct RAYA_HANDLE_CHAIN {
    HANDLE *handles;
    size_t count;
} RAYA_HANDLE_CHAIN;

/* Opens every component as a directory relative to the preceding pinned HANDLE.
   The caller must pass an empty chain and close it with raya_close_handle_chain.
   The root HANDLE is borrowed. */
BOOL raya_open_directory_chain(HANDLE root, const wchar_t *relative_path,
                               RAYA_HANDLE_CHAIN *chain);
void raya_close_handle_chain(RAYA_HANDLE_CHAIN *chain);

/* Opens one final regular file relative to a pinned parent directory HANDLE.
   On success, ownership of *file_handle transfers to the caller. */
BOOL raya_open_single_link_regular_file(HANDLE parent, const wchar_t *name,
                                        ULONGLONG maximum_size,
                                        HANDLE *file_handle, ULONGLONG *file_size);

#endif
