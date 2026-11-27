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

typedef struct RAYA_FILE_INFO {
    ULONGLONG size;
    ULONGLONG link_count;
    DWORD attributes;
    FILETIME creation_time;
    FILETIME last_access_time;
    FILETIME last_write_time;
    bool directory;
    bool reparse_point;
    bool delete_pending;
} RAYA_FILE_INFO;

typedef struct RAYA_REQUEST_CONTEXT {
    RAYA_ROOT_REGISTRY *registry;
    HANDLE root_handle;       /* Borrowed from registry while active. */
    HANDLE directory_handle;  /* Borrowed root or last HANDLE in directory_chain. */
    HANDLE file_handle;       /* Owned by this context for file operations. */
    ULONGLONG file_size;
    uint64_t offset;
    uint32_t length;
    uint16_t operation;
    SRWLOCK io_lock;
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

/* Encodes configured root IDs only: u16 count, then repeated u8 length + ASCII ID.
   Absolute filesystem paths are never returned by this protocol operation. */
BOOL raya_root_registry_encode_root_ids(
    RAYA_ROOT_REGISTRY *registry,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *written);

/* Resolves request.root_id to a configured pinned root HANDLE, then acquires
   request-scoped directory/file HANDLEs relative to that root. READ_FILE,
   GET_FILE_INFO, READ_FILE_CHUNK and READ_FILE_TAIL open the final regular file;
   LIST_DIRECTORY pins the requested directory. Root-level LIST_DIRECTORY gets
   an independent root HANDLE view validated by volume serial and file ID. */
BOOL raya_request_context_begin(
    RAYA_ROOT_REGISTRY *registry,
    const raya_request *request,
    ULONGLONG maximum_file_size,
    RAYA_REQUEST_CONTEXT *context);

/* Reads READ_FILE, READ_FILE_CHUNK or READ_FILE_TAIL using the same validated
   file HANDLE. The request's range is checked against the opened file size and
   the 256 KiB response limit. Calls on one context are serialized. */
BOOL raya_request_context_read(
    RAYA_REQUEST_CONTEXT *context,
    void *buffer,
    size_t buffer_capacity,
    DWORD *bytes_read);

/* Returns metadata queried from the same validated file HANDLE. */
BOOL raya_request_context_get_file_info(
    RAYA_REQUEST_CONTEXT *context,
    RAYA_FILE_INFO *info);

/* Closes owned file/chain HANDLEs and releases the registry active-context ref.
   The caller must stop and join all users of this context before close; the
   per-context lock serializes read/metadata calls, not context destruction. */
BOOL raya_request_context_close(RAYA_REQUEST_CONTEXT *context);

#endif
