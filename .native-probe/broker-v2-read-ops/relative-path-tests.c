#include "relative-path.h"
#include <stdio.h>

typedef struct { const wchar_t *path; BOOL expected; const char *name; } CASE;

int wmain(void)
{
    static const CASE cases[] = {
        { L"file.txt", TRUE, "single component" },
        { L"folder\\file.txt", TRUE, "nested path" },
        { L"a\\b\\c.bin", TRUE, "three components" },
        { L"", FALSE, "empty" },
        { L"\\rooted.txt", FALSE, "rooted" },
        { L"\\\\server\\share", FALSE, "UNC" },
        { L"C:\\file.txt", FALSE, "drive qualified" },
        { L"folder/file.txt", FALSE, "alternate separator" },
        { L"folder\\..\\secret", FALSE, "dot dot" },
        { L".\\secret", FALSE, "dot component" },
        { L"folder\\\\file", FALSE, "empty component" },
        { L"folder\\", FALSE, "trailing separator" },
        { L"folder.\\file", FALSE, "trailing dot" },
        { L"folder \\file", FALSE, "trailing space" },
        { L"NUL", FALSE, "NUL device" },
        { L"con.txt", FALSE, "CON extension" },
        { L"Com1.log", FALSE, "COM device" },
        { L"lpt9", FALSE, "LPT device" },
        { L"CONIN$", FALSE, "console input device" },
        { L"conout$.txt", FALSE, "console output device" },
        { L"CLOCK$", FALSE, "clock device" },
        { L"COM\x00b9", FALSE, "COM superscript device" },
        { L"safe:stream", FALSE, "ADS" },
        { L"wild?.txt", FALSE, "wildcard" },
        { L"control\tchar", FALSE, "control character" }
    };
    unsigned failures = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        size_t count = 0;
        BOOL actual = raya_validate_relative_path(cases[i].path, &count);
        if (actual != cases[i].expected) {
            printf("FAIL %s expected=%d actual=%d\\n", cases[i].name,
                   (int)cases[i].expected, (int)actual);
            ++failures;
        } else {
            printf("PASS %s\\n", cases[i].name);
        }
        if (actual && count == 0) ++failures;
    }
    if (failures) { printf("RELATIVE_PATH_TESTS_FAIL %u\\n", failures); return 1; }
    printf("RELATIVE_PATH_TESTS_PASS %zu\\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
