#ifndef RAYA_ROOT_REGISTRY_H
#define RAYA_ROOT_REGISTRY_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "handle-chain.h"
#include "read-frame.h"

#define RAYA_ROOT_REGISTRY_MAX_ROOTS 8u

typedef struct RAYA_ROOT_CONFIG {
    const char *root_id;
    const wchar_t *absolute_path;
} RAYA_ROOT_CONFIG;

typedef struct RAYA_ROOT_ENTRY {
    char root_id[RAYA_MAX_ROOT_ID_BYTES + 1u];
    HANDLE root_handle;
} RAYA_ROOT_ENTRY;

typedef struct RAYA_ROOT_REGISTRY {
    SRWLOCK lock;
    RAYA_ROOT_ENTRY entries[RAYA_ROOT_REGISTRY_MAX_ROOTS];
    size_t count;
    size_t active_contexts;
    bool initialized;
    bool closing;
} RAYA_ROOT_REGISTRY;

typedef struct RAYA_REQUEST_CONTEXT {
    RAYA_ROOT_REGISTRY *registry;
    HANDLE root_handle;       /* Borrowed from registry while active. */
    HANDLE directory_handle;  /* Borrowed root or last HANDLE in directory_chain. */
    HANDLE file_handle;       /* Owned by this context for READ_FILE. */
    ULONGLONG file_size;
    uint16_t operation;
    RAYA_HANDLE_CHAIN directory_chain;
    bool active;
} RAYA_REQUEST_CONTEXT;

/* Configured paths are trusted service configuration, never request input.
   Registry and context objects must be zero-initialized before first use. */
BOOL raya_root_registry_init(
    RAYA_ROOT_REGISTRY *registry,
    const RAYA_ROOT_CONFIG *configs,
    size_t config_count);

/* Refuses to close while any request context is active. */
BOOL raya_root_registry_close(RAYA_ROOT_REGISTRY *registry);

/* Resolves request.root_id to a configured pinned root HANDLE, then acquires
   request-scoped directory/file HANDLEs relative to that root. READ_FILE opens
   the final regular file; LIST_DIRECTORY pins the requested directory. */
BOOL raya_request_context_begin(
    RAYA_ROOT_REGISTRY *registry,
    const raya_request *request,
    ULONGLONG maximum_file_size,
    RAYA_REQUEST_CONTEXT *context);

/* Closes owned file/chain HANDLEs and releases the registry active-context ref. */
BOOL raya_request_context_close(RAYA_REQUEST_CONTEXT *context);

#endif
