#include "relative-path.h"
#include <wchar.h>

static BOOL is_separator(wchar_t ch) { return ch == L'\\'; }

static wchar_t ascii_upper(wchar_t ch)
{
    return ch >= L'a' && ch <= L'z' ? (wchar_t)(ch - (L'a' - L'A')) : ch;
}

static BOOL ascii_equal_fold(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b) if (ascii_upper(*a++) != ascii_upper(*b++)) return FALSE;
    return *a == L'\0' && *b == L'\0';
}

static BOOL reserved_device_component(const wchar_t *s, size_t n)
{
    size_t base = 0;
    wchar_t name[7] = {0};
    while (base < n && s[base] != L'.') ++base;
    if (base == 0 || base >= sizeof(name) / sizeof(name[0])) return FALSE;
    for (size_t i = 0; i < base; ++i) name[i] = ascii_upper(s[i]);
    if (ascii_equal_fold(name, L"CON") || ascii_equal_fold(name, L"PRN") ||
        ascii_equal_fold(name, L"AUX") || ascii_equal_fold(name, L"NUL") ||
        ascii_equal_fold(name, L"CLOCK$") || ascii_equal_fold(name, L"CONIN$") ||
        ascii_equal_fold(name, L"CONOUT$")) return TRUE;
    if (base == 4 &&
        ((name[0] == L'C' && name[1] == L'O' && name[2] == L'M') ||
         (name[0] == L'L' && name[1] == L'P' && name[2] == L'T')) &&
        ((name[3] >= L'1' && name[3] <= L'9') ||
         name[3] == 0x00b9 || name[3] == 0x00b2 || name[3] == 0x00b3)) return TRUE;
    return FALSE;
}

static BOOL valid_component(const wchar_t *s, size_t n)
{
    if (n == 0 || n > 255) return FALSE;
    if ((n == 1 && s[0] == L'.') ||
        (n == 2 && s[0] == L'.' && s[1] == L'.')) return FALSE;
    if (s[n - 1] == L'.' || s[n - 1] == L' ') return FALSE;
    for (size_t i = 0; i < n; ++i) {
        wchar_t ch = s[i];
        if (ch < 32 || ch == L'/' || ch == L':' || ch == L'<' ||
            ch == L'>' || ch == L'\"' || ch == L'|' || ch == L'?' ||
            ch == L'*') return FALSE;
    }
    return !reserved_device_component(s, n);
}

BOOL raya_validate_relative_path(const wchar_t *path, size_t *component_count)
{
    size_t length, start, count = 0;
    if (component_count) *component_count = 0;
    if (path == NULL || path[0] == L'\\' || path[0] == L'/' ||
        path[0] == L'\0') return FALSE;
    length = wcslen(path);
    if (length == 0 || length > 32760) return FALSE;
    start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || is_separator(path[i])) {
            if (!valid_component(path + start, i - start)) return FALSE;
            ++count;
            start = i + 1;
        }
    }
    if (component_count) *component_count = count;
    return count > 0;
}
