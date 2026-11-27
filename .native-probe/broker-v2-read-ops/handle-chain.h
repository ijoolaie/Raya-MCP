#ifndef RAYA_BROKER_V2_HANDLE_CHAIN_H
#define RAYA_BROKER_V2_HANDLE_CHAIN_H
#include <windows.h>
#include <stddef.h>

typedef struct RAYA_HANDLE_CHAIN {
    HANDLE *handles;
    size_t count;
} RAYA_HANDLE_CHAIN;

typedef struct RAYA_DIRECTORY_ENTRY {
    wchar_t name[256];
    DWORD attributes;
    ULONGLONG file_id;
    ULONGLONG size;
    ULONGLONG allocation_size;
    LONGLONG creation_time;
    LONGLONG last_access_time;
    LONGLONG last_write_time;
    LONGLONG change_time;
} RAYA_DIRECTORY_ENTRY;

typedef struct RAYA_DIRECTORY_PAGE {
    RAYA_DIRECTORY_ENTRY *entries;
    size_t count;
    BOOL done;
} RAYA_DIRECTORY_PAGE;

/* Opens every component as a directory relative to the preceding pinned HANDLE.
   The caller must pass an empty chain and close it with raya_close_handle_chain.
   The root HANDLE is borrowed. */
BOOL raya_open_directory_chain(HANDLE root, const wchar_t *relative_path,
                               RAYA_HANDLE_CHAIN *chain);
void raya_close_handle_chain(RAYA_HANDLE_CHAIN *chain);

/* Enumerates one bounded page using only the supplied already-authorized
   directory HANDLE. Reparse entries are reported as metadata, never traversed.
   The page must be zero-initialized before first use. Serialize calls on a
   given directory HANDLE; its cursor is shared and not a snapshot. Child names
   are display/enumeration data only; reopen relative to this HANDLE and revalidate.
   On ERROR_MORE_DATA the
   OS cursor may have advanced; restart with restart=TRUE and a larger cap. */
BOOL raya_enumerate_directory_page(HANDLE directory, size_t max_entries,
                                   BOOL restart, RAYA_DIRECTORY_PAGE *page);
void raya_free_directory_page(RAYA_DIRECTORY_PAGE *page);

/* Opens one final regular file relative to a pinned parent directory HANDLE.
   On success, ownership of *file_handle transfers to the caller. */
BOOL raya_open_single_link_regular_file(HANDLE parent, const wchar_t *name,
                                        ULONGLONG maximum_size,
                                        HANDLE *file_handle, ULONGLONG *file_size);

#endif
