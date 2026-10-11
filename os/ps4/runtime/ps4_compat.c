// Replacements for libkernel functions this firmware may not provide.
//
// A call to a function the PS4's kernel library doesn't export ends the app on the spot (no
// signal, no crash report). A test app that imported shm_open, shm_unlink, sysctlbyname, msync and
// fdatasync was refused before main(), so at least one of that group is missing; flock was never
// tested. The game reaches these while loading (file writes, mapped files), so they are routed
// here with --wrap (cmake/PS4Runtime.cmake). Each logs its first call to the boot trace.

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

void ps4_boot_trace(const char* stage);  // ps4_crashlog.cpp

static void first_call(volatile int* seen, const char* what) {
    if (!*seen) {
        *seen = 1;
        ps4_boot_trace(what);
    }
}

// Same guarantee for file data (fsync also writes metadata).
int __wrap_fdatasync(int fd) {
    static volatile int seen;
    first_call(&seen, "fdatasync -> fsync");
    return fsync(fd);
}

// PS4 mappings aren't backed by files the kernel writes back; nothing to flush.
int __wrap_msync(void* address, size_t length, int flags) {
    static volatile int seen;
    (void)address;
    (void)length;
    (void)flags;
    first_call(&seen, "msync -> no-op");
    return 0;
}

// Callers treat a failed lookup as "unknown".
int __wrap_sysctlbyname(const char* name, void* old_value, size_t* old_length, const void* new_value,
                        size_t new_length) {
    static volatile int seen;
    (void)name;
    (void)old_value;
    (void)old_length;
    (void)new_value;
    (void)new_length;
    first_call(&seen, "sysctlbyname -> ENOENT");
    errno = ENOENT;
    return -1;
}

// One process owns /data/LibertyRecomp; advisory locks have nothing to exclude.
int __wrap_flock(int fd, int operation) {
    static volatile int seen;
    (void)fd;
    (void)operation;
    first_call(&seen, "flock -> no-op");
    return 0;
}

// The recompiled PowerPC code computes fmadd/fmsub/fnmadd/fnmsub (and their single forms) with
// std::fma, ~14,600 call sites. The PS4's Jaguar cores have no FMA unit, so each became a call
// into libm's exact software fma (fma + normalize: the hottest code in loading-screen stack
// dumps). A separate multiply and add instead: the product isn't rounded before the add on the
// Xbox 360, here it is (one extra rounding in the last bit), which games don't depend on.
#pragma STDC FP_CONTRACT OFF
double __wrap_fma(double a, double b, double c) {
    return a * b + c;
}

float __wrap_fmaf(float a, float b, float c) {
    // Exact product in double (24 + 24 bits), one rounding to float.
    return (float)((double)a * (double)b + (double)c);
}
