PS4 runtime support for the OpenOrbis build, taken from DolphinPS4's `port/`
directory (proven on retail consoles), with log paths moved to
`/data/LibertyRecomp`. Wired up by `cmake/PS4Runtime.cmake`.

- `ps4_runtime.cpp`, `ps4_dlmalloc.c`, `third_party/dlmalloc/malloc.c`: app heap.
  OpenOrbis' libc heap cannot initialise on retail consoles, so the malloc
  family is routed here with `--wrap`, and `ps4_crt1.S` (a crt1.o replacement)
  sends system-module allocations here too. Also fixes for the kernel's
  `pthread_once`, large anonymous `mmap`s and a TSC-based `clock_gettime`.
- `ps4_crashlog.cpp`: boot milestones in `/data/LibertyRecomp/boot-trace.log`
  and fatal-signal reports in `/data/LibertyRecomp/crash.log`.
- `ps4_string.c`: SSE2 memcpy/memmove/memset/memcmp replacing musl's byte loops.
