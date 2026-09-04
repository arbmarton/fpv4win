#pragma once

// The vendored rtl8812au driver sources under 3rd/ use the Windows spellings of a few
// basic types and constants, which come from windows.h in the MSVC build. Nothing
// provides them elsewhere, so define them here; this header is force-included into
// every translation unit on non-Windows platforms (see CMakeLists.txt).

#ifndef _WIN32

#include <stdbool.h>

typedef bool BOOLEAN;
typedef void VOID;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#endif // !_WIN32
