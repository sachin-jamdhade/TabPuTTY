#include <windows.h>
#include <stdio.h>

void log_trace(const char *msg) {
    FILE *f = fopen("putty_trace.log", "a");
    if (f) {
        fprintf(f, "[PID %lu] %s\n", GetCurrentProcessId(), msg);
        fclose(f);
    }
}
