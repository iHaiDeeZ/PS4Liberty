// Crash diagnostics for PS4 test builds.
//
// PS4 coredumps are encrypted and klog is shared with other tools, so the app reports its own
// crashes: a constructor that runs before all others (priority 101) starts
// /data/LibertyRecomp/boot-trace.log and installs a handler for fatal signals that appends the
// signal, fault address, registers and the code addresses found on the stack to
// /data/LibertyRecomp/crash.log. Addresses are printed relative to the start of .text, i.e. as ELF
// virtual addresses: symbolize with `llvm-symbolizer --obj=<elf> 0x...`.
//
// Handlers installed later (Dolphin's fastmem handler) chain to this one for faults they don't
// handle.

#include <fcntl.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cxxabi.h>
#include <exception>
#include <typeinfo>

extern "C" char __text_start[];
extern "C" void ps4_install_signal_stack();  // defined by the OpenOrbis link.x at the start of .text

namespace {

// FreeBSD values (the PS4 kernel's); the OpenOrbis musl headers may carry Linux ones.
constexpr int kSigIll = 4, kSigAbrt = 6, kSigFpe = 8, kSigBus = 10, kSigSegv = 11;
// Signals that end the process without being faults: logged, then re-raised.
// HUP INT QUIT TRAP EMT SYS PIPE ALRM TERM XCPU XFSZ VTALRM PROF
constexpr int kOtherFatalSignals[] = {1, 2, 3, 5, 7, 12, 13, 14, 15, 24, 25, 26, 27};
constexpr int kSaSiginfo = 0x40, kSaOnstack = 0x1;
constexpr size_t kSignalStackSize = 64 * 1024;
constexpr int kMcontextOffset = 0x40;  // measured on hardware
// Registers in FreeBSD mcontext order (after mc_onstack).
constexpr const char* kRegNames[] = {"rdi", "rsi", "rdx", "rcx", "r8",  "r9",  "rax", "rbx",
                                     "rbp", "r10", "r11", "r12", "r13", "r14", "r15"};
constexpr int kRipIndex = 20, kRspIndex = 23;
constexpr uintptr_t kTextWindow = 0x4000000;  // 64 MiB covers the eboot's code

int g_traceFd = -1;

// Opened on first use: the heap may log before this file's constructor runs.
int traceFd() {
    if (g_traceFd < 0) {
        mkdir("/data/LibertyRecomp", 0777);
        g_traceFd = open("/data/LibertyRecomp/boot-trace.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    return g_traceFd;
}

void writeAll(int fd, const char* text) {
    size_t length = strlen(text);
    while (length > 0) {
        ssize_t written = write(fd, text, length);
        if (written <= 0)
            return;
        text += written;
        length -= static_cast<size_t>(written);
    }
}

__attribute__((format(printf, 2, 3))) void writeFormat(int fd, const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    writeAll(fd, line);
}

bool isCode(uint64_t address) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(__text_start);
    return address >= base && address < base + kTextWindow;
}

uint64_t toElf(uint64_t address) { return address - reinterpret_cast<uintptr_t>(__text_start); }

void setHandler(int sig, void (*handler)(int, siginfo_t*, void*), int flags) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    // The handler union is the first member; its member names differ between headers.
    memcpy(&action, &handler, sizeof(handler));
    action.sa_flags = flags;
    sigemptyset(&action.sa_mask);
    sigaction(sig, &action, nullptr);
}

// How many 8-byte words above `rsp` (at most `limit`) still lie in the current thread's stack.
int stackWordsAbove(uint64_t rsp, int limit) {
    pthread_attr_t attr;
    void* base = nullptr;
    size_t size = 0;
    if (pthread_attr_init(&attr) != 0)
        return 256;
    const bool known = pthread_attr_get_np(pthread_self(), &attr) == 0 &&
                       pthread_attr_getstack(&attr, &base, &size) == 0 && base;
    pthread_attr_destroy(&attr);
    if (!known)
        return 256;
    const uint64_t top = reinterpret_cast<uint64_t>(base) + size;
    if (rsp >= top || rsp < reinterpret_cast<uint64_t>(base))
        return 0;  // not on this thread's stack (e.g. the signal stack)
    const uint64_t words = (top - rsp) / 8;
    return words < static_cast<uint64_t>(limit) ? static_cast<int>(words) : limit;
}

void crashHandler(int sig, siginfo_t* info, void* context) {
    static volatile int s_depth = 0;
    // Crashed while reporting: end the process. Returning would re-run the faulting read
    // forever (a stack scan past the top of the stack did that, and the console froze).
    if (s_depth++ > 0)
        _exit(128 + sig);

    const int fd = open("/data/LibertyRecomp/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        const auto* regs = reinterpret_cast<const uint64_t*>(static_cast<char*>(context) + kMcontextOffset);
        const uint64_t rip = regs[kRipIndex];
        const uint64_t rsp = regs[kRspIndex];
        const uint64_t address = *reinterpret_cast<const uint64_t*>(reinterpret_cast<char*>(info) + 24);
        writeFormat(fd, "\n=== signal %d (code %d), fault address 0x%llx\n", sig,
                    *reinterpret_cast<const int*>(reinterpret_cast<char*>(info) + 8),
                    static_cast<unsigned long long>(address));
        writeFormat(fd, "rip 0x%llx%s elf 0x%llx, rsp 0x%llx, text at %p\n",
                    static_cast<unsigned long long>(rip), isCode(rip) ? "" : " (not eboot code)",
                    static_cast<unsigned long long>(toElf(rip)), static_cast<unsigned long long>(rsp),
                    static_cast<void*>(__text_start));
        for (int i = 0; i < 15; i++)
            writeFormat(fd, "%s 0x%llx%s", kRegNames[i], static_cast<unsigned long long>(regs[1 + i]),
                        i % 4 == 3 ? "\n" : "  ");
        writeAll(fd, "\ncode addresses on the stack (elf):\n");
        const auto* stack = reinterpret_cast<const uint64_t*>(rsp);
        const int words = stackWordsAbove(rsp, 2048);
        int found = 0;
        for (int i = 0; i < words && found < 48; i++) {
            if (isCode(stack[i])) {
                writeFormat(fd, "  [rsp+0x%x] 0x%llx\n", i * 8, static_cast<unsigned long long>(toElf(stack[i])));
                found++;
            }
        }
        close(fd);
    }
    if (traceFd() >= 0)
        writeAll(traceFd(), "crashed (see crash.log)\n");

    // End the process here. Handing the signal back to the system (default action, re-raise)
    // left a SIGABRT'd LibertyRecomp running for 100 s with the GPU and display held, and the
    // console froze.
    _exit(128 + sig);
}

// std::terminate (e.g. an uncaught exception): record what was thrown, then abort as usual.
[[noreturn]] void terminateHandler() {
    char message[512] = "std::terminate called";
    if (const std::type_info* type = abi::__cxa_current_exception_type()) {
        int status = 0;
        // Type only: Dolphin builds with -fno-exceptions, so what() can't be fetched here.
        char* name = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
        snprintf(message, sizeof(message), "uncaught exception %s", name ? name : type->name());
        // This app builds with exceptions, so the message is available too.
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& exception) {
            const size_t used = strlen(message);
            snprintf(message + used, sizeof(message) - used, ": %s", exception.what());
        } catch (...) {
        }
    }
    const int fd = open("/data/LibertyRecomp/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        writeFormat(fd, "\n=== %s\n", message);
        close(fd);
    }
    if (traceFd() >= 0)
        writeFormat(traceFd(), "%s\n", message);
    abort();
}

extern "C" int32_t sceKernelAvailableFlexibleMemorySize(size_t* size);
extern "C" int32_t sceKernelAvailableDirectMemorySize(off_t search_start, off_t search_end,
                                                      size_t alignment, off_t* physical,
                                                      size_t* size);
extern "C" size_t sceKernelGetDirectMemorySize();

// Free flexible memory, and the largest free direct-memory block (MiB).
void memoryStatus(size_t& flexible_mib, size_t& direct_mib) {
    size_t flexible = 0;
    sceKernelAvailableFlexibleMemorySize(&flexible);
    off_t physical = 0;
    size_t direct = 0;
    sceKernelAvailableDirectMemorySize(0, static_cast<off_t>(sceKernelGetDirectMemorySize()), 64 * 1024,
                                       &physical, &direct);
    flexible_mib = flexible >> 20;
    direct_mib = direct >> 20;
}

// /data/LibertyRecomp/heartbeat.txt: process uptime and free memory, rewritten every 250 ms. When
// the app disappears without a trace (the system killing it, e.g. out of memory or for a hung
// GPU), it tells how long it lived and how much memory was left. memory.log keeps the history
// (every 5 s), so a steady decline before the end stands out.
extern "C" void ps4_dump_thread_stacks(const char* path, const char* reason);
extern "C" void ps4_stat_report(int fd);
extern "C" void ps4_thread_cpu_report(int fd);
extern "C" __attribute__((weak)) int rex_gta4_native_profile_start();
extern "C" void ps4_boot_trace(const char* stage);

void startHeartbeat() {
    pthread_t thread;
    pthread_create(
        &thread, nullptr,
        [](void*) -> void* {
            const int fd = open("/data/LibertyRecomp/heartbeat.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            const int history = open("/data/LibertyRecomp/memory.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0)
                return nullptr;
            timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            size_t flexible = 0, direct = 0;
            for (unsigned tick = 0;; ++tick) {
                timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                const double seconds = static_cast<double>(now.tv_sec - start.tv_sec) +
                                       static_cast<double>(now.tv_nsec - start.tv_nsec) / 1e9;
                if (tick % 4 == 0)
                    memoryStatus(flexible, direct);
                char line[128];
                const int length = snprintf(line, sizeof(line),
                                            "alive %10.3f s, flexible %5zu MiB, direct %5zu MiB\n",
                                            seconds, flexible, direct);
                pwrite(fd, line, static_cast<size_t>(length), 0);
                if (history >= 0 && tick % 20 == 0) {
                    write(history, line, static_cast<size_t>(length));
                    ps4_stat_report(history);
                    ps4_thread_cpu_report(history);
                }
                // On request (an empty /data/LibertyRecomp/dump-stacks, e.g. over FTP): every
                // thread's stack to stacks.txt (ps4_stackdump.cpp).
                // An empty /data/LibertyRecomp/profile-start starts a native renderer profile
                // capture (graphics_system.cpp; needs the native-profiler diagnostics category).
                if (tick % 4 == 0 && unlink("/data/LibertyRecomp/profile-start") == 0 &&
                    rex_gta4_native_profile_start) {
                    rex_gta4_native_profile_start();
                    ps4_boot_trace("native profile capture requested");
                }
                if (tick % 4 == 0 && unlink("/data/LibertyRecomp/dump-stacks") == 0) {
                    char reason[64];
                    snprintf(reason, sizeof(reason), "requested at %.1f s", seconds);
                    ps4_dump_thread_stacks("/data/LibertyRecomp/stacks.txt", reason);
                }
                usleep(250 * 1000);
            }
        },
        nullptr);
    pthread_detach(thread);
}

__attribute__((constructor(101))) void earlyInit() {
    if (traceFd() >= 0)
        writeFormat(traceFd(), "static init (text at %p)\n", static_cast<void*>(__text_start));
    std::set_terminate(terminateHandler);
    // A quiet exit() (e.g. an init failure) otherwise looks like a crash with no crash.log.
    atexit([] {
        if (traceFd() >= 0)
            writeAll(traceFd(), "exit() called\n");
    });
    const int signals[] = {kSigIll, kSigAbrt, kSigFpe, kSigBus, kSigSegv};
    for (int sig : signals)
        setHandler(sig, crashHandler, kSaSiginfo | kSaOnstack);
    for (int sig : kOtherFatalSignals)
        setHandler(sig, crashHandler, kSaSiginfo | kSaOnstack);
    startHeartbeat();
    ps4_install_signal_stack();

    pthread_attr_t attr;
    size_t stackSize = 0;
    if (pthread_attr_init(&attr) == 0) {
        if (pthread_attr_get_np(pthread_self(), &attr) == 0)
            pthread_attr_getstacksize(&attr, &stackSize);
        pthread_attr_destroy(&attr);
    }
    if (traceFd() >= 0)
        writeFormat(traceFd(), "main thread stack %zu KiB\n", stackSize / 1024);
}

}  // namespace

// Gives the calling thread its own stack for signal handlers, so a stack overflow (a fault on
// the guard page) still reaches crashHandler instead of killing the process silently.
extern "C" void ps4_install_signal_stack() {
    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = malloc(kSignalStackSize);
    stack.ss_size = kSignalStackSize;
    if (stack.ss_sp && sigaltstack(&stack, nullptr) != 0 && traceFd() >= 0)
        writeAll(traceFd(), "sigaltstack failed\n");
}

// Appends "<what>: callers 0x.. 0x.." to the boot trace: code addresses found on the calling
// thread's stack (ELF addresses, symbolize like crash.log). A cheap "who calls this" without
// frame pointers; may include stale return addresses.
extern "C" void ps4_boot_trace(const char* stage);
extern "C" __attribute__((noinline)) void ps4_trace_callers(const char* what) {
    // Read from the stack pointer itself: walking past a local variable is undefined behaviour
    // the optimizer is free to drop (an earlier version printed nothing).
    const uint64_t* stack;
    asm volatile("mov %%rsp, %0" : "=r"(stack));
    char line[320];
    int len = snprintf(line, sizeof(line), "%s: callers", what);
    int found = 0;
    for (int i = 0; i < 512 && found < 10 && len < 280; i++) {
        if (isCode(stack[i])) {
            len += snprintf(line + len, sizeof(line) - len, " 0x%llx",
                            static_cast<unsigned long long>(toElf(stack[i])));
            found++;
        }
    }
    ps4_boot_trace(line);
}

extern "C" [[noreturn]] void __real_abort();
extern "C" [[noreturn]] void __real__exit(int code);

extern "C" [[noreturn]] void __wrap_abort() {
    ps4_trace_callers("abort() called");
    const int fd = open("/data/LibertyRecomp/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        writeAll(fd, "\n=== abort() called (callers in boot-trace.log)\n");
        close(fd);
    }
    __real_abort();
}

extern "C" [[noreturn]] void __wrap__exit(int code) {
    char what[64];
    snprintf(what, sizeof(what), "_exit(%d) called", code);
    ps4_trace_callers(what);
    __real__exit(code);
}

// Appends a line to /data/LibertyRecomp/boot-trace.log (startup milestones), prefixed with the
// seconds since the first trace line.
extern "C" void ps4_boot_trace(const char* stage) {
    const int fd = traceFd();
    if (fd < 0)
        return;
    static timespec s_start;
    static bool s_started = false;
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!s_started) {
        s_start = now;
        s_started = true;
    }
    const double seconds = static_cast<double>(now.tv_sec - s_start.tv_sec) +
                           static_cast<double>(now.tv_nsec - s_start.tv_nsec) / 1e9;
    // One write per line, so lines from different threads don't interleave.
    // Room for the monitor's long lines; a cut line still ends with its newline (a truncated
    // monitor line used to run into the next one).
    char line[2048];
    const size_t length = strlen(stage);
    const bool newline = length == 0 || stage[length - 1] != '\n';
    const int written =
        snprintf(line, sizeof(line), "[%8.3f] %s%s", seconds, stage, newline ? "\n" : "");
    if (written >= static_cast<int>(sizeof(line)))
        line[sizeof(line) - 2] = '\n';
    writeAll(fd, line);
}
