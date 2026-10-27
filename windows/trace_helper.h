#ifndef TRACE_HELPER_H
#define TRACE_HELPER_H

#ifdef TABPUTTY_TRACE
#include <windows.h>
#include <stdio.h>

static inline void log_trace(const char *msg) {
    FILE *f = fopen("putty_trace.log", "a");
    if (f) {
        fprintf(f, "[PID %lu] %s\n", GetCurrentProcessId(), msg);
        fclose(f);
    }
}
#define TRACE_LOG(msg) log_trace(msg)
#else
#define log_trace(msg) ((void)0)
#define TRACE_LOG(msg) ((void)0)
#endif

#endif /* TRACE_HELPER_H */
