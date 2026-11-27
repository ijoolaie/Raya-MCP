#ifndef RAYA_BROKER_V2_RELATIVE_PATH_H
#define RAYA_BROKER_V2_RELATIVE_PATH_H
#include <windows.h>
#include <stddef.h>

/* Lexical gate only. This does not open or authorize filesystem objects. */
BOOL raya_validate_relative_path(const wchar_t *path, size_t *component_count);

#endif
