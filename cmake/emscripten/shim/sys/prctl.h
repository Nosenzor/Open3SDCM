// Minimal <sys/prctl.h> shim for the Emscripten build.
//
// Poco 1.14 detects Emscripten as Linux (its Platform.h has no Emscripten
// case) and compiles Foundation's Thread_POSIX.cpp, which includes
// <sys/prctl.h> for thread naming. Emscripten has no prctl syscall. Thread
// naming is never exercised by this project (DCM parsing is single
// threaded), so the shim just satisfies the include and fails prctl()
// calls at runtime like the real syscall would for an unsupported option.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif
#ifndef PR_GET_NAME
#define PR_GET_NAME 16
#endif

#ifdef __cplusplus
inline int prctl(int option, ...)
{
    (void)option;
    return -1;
}
#else
#include <stdarg.h>
inline int prctl(int option, ...)
{
    (void)option;
    return -1;
}
#endif

#ifdef __cplusplus
}
#endif
