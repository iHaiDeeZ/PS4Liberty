// Thread stack dumps on demand (from DolphinPS4's ps4_sampler.cpp hang dumps).
//
// Every thread created through rex::thread registers itself (ps4_watch_thread, named later by
// ps4_name_watched_thread). ps4_dump_thread_stacks() signals each one; the handler records its
// instruction pointer and every eboot code address on its stack (return addresses, newest first),
// and whether it is blocked in _umtx_op (mutex / condition variable / semaphore waits).
//
// Trigger without a debugger: create /data/LibertyRecomp/dump-stacks (an empty file, over FTP);
// the heartbeat thread (ps4_crashlog.cpp) sees it, deletes it and appends a dump to
// /data/LibertyRecomp/stacks.txt. Symbolize with llvm-symbolizer --obj=<unstripped elf>
// (addresses are relative to .text, as in crash.log).

#include <algorithm>
#include <atomic>
#include <new>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern "C" char __text_start[];               // OpenOrbis link.x: start of .text
extern "C" int pthread_getthreadid_np(void);  // libkernel (FreeBSD libthr)
extern "C" int32_t sceKernelQueryMemoryProtection(void* address, void** start, void** end,
                                                  uint32_t* protection);

namespace {

constexpr int kSigDump = 30;  // SIGUSR1 (FreeBSD)
constexpr int kSaSiginfo = 0x40, kSaRestart = 0x0002;
constexpr int kMcontextOffset = 0x40;  // measured on hardware (ps4_crashlog.cpp)
constexpr int kRipIndex = 20, kRspIndex = 23;
constexpr uint64_t kSystemLibs = 0x800000000ULL;
constexpr uintptr_t kTextWindow = 0x10000000;
constexpr int kMaxWatched = 512, kMaxFrames = 64;
constexpr clockid_t kThreadCpuClock = 14;   // FreeBSD CLOCK_THREAD_CPUTIME_ID
constexpr clockid_t kProcessCpuClock = 15;  // FreeBSD CLOCK_PROCESS_CPUTIME_ID

struct WatchedThread {
    std::atomic<pthread_t> thread{};
    char name[48] = {};
    uintptr_t stack_limit = 0;  // highest stack address the scan may read
    int tid = 0;
    uint64_t rip = 0;
    uint64_t cpu_ns = 0;  // the thread's CPU time when it was dumped
    uint64_t reported_cpu_ns = 0;  // cpu_ns at the last ps4_thread_cpu_report
    uint64_t frames[kMaxFrames];
    int frame_count = 0;
    bool in_umtx = false;
    uint64_t umtx_obj = 0, umtx_op = 0, umtx_val = 0, umtx_obj2 = 0;
    std::atomic<int> done{0};
};
WatchedThread g_watched[kMaxWatched];
std::atomic<int> g_watched_count{0};

void writeLine(int fd, const char* text) {
    const size_t length = strlen(text);
    size_t written = 0;
    while (written < length) {
        const ssize_t n = write(fd, text + written, length - written);
        if (n <= 0)
            return;
        written += static_cast<size_t>(n);
    }
}

void dumpHandler(int, siginfo_t*, void* context) {
    const pthread_t self = pthread_self();
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    const int count = std::min(g_watched_count.load(std::memory_order_acquire), kMaxWatched);
    for (int i = 0; i < count; i++) {
        WatchedThread& t = g_watched[i];
        if (t.thread.load(std::memory_order_relaxed) != self)
            continue;
        const auto* regs =
            reinterpret_cast<const uint64_t*>(static_cast<char*>(context) + kMcontextOffset);
        t.rip = regs[kRipIndex];
        timespec cpu = {};
        clock_gettime(kThreadCpuClock, &cpu);
        t.cpu_ns = uint64_t(cpu.tv_sec) * 1000000000ull + uint64_t(cpu.tv_nsec);
        // libkernel's _umtx_op stub ("mov $0x1c6, %rax; mov %rcx, %r10; syscall"): the thread
        // sits right after the syscall; FreeBSD mcontext: rdi 1, rsi 2, rdx 3, r10 10.
        t.in_umtx = false;
        if (t.rip >= kSystemLibs && t.rip < 2 * kSystemLibs) {
            const auto* code = reinterpret_cast<const uint8_t*>(t.rip - 12);
            static const uint8_t kUmtxStub[12] = {0x48, 0xc7, 0xc0, 0xc6, 0x01, 0x00,
                                                  0x00, 0x49, 0x89, 0xca, 0x0f, 0x05};
            if (memcmp(code, kUmtxStub, sizeof(kUmtxStub)) == 0) {
                t.in_umtx = true;
                t.umtx_obj = regs[1];
                t.umtx_op = regs[2];
                t.umtx_val = regs[3];
                t.umtx_obj2 = regs[10];
            }
        }
        const uint64_t rsp = regs[kRspIndex];
        const bool same_thread = pthread_getthreadid_np() == t.tid;
        const bool sane = t.stack_limit > rsp && t.stack_limit - rsp <= (4u << 20);
        const auto* stack = reinterpret_cast<const uint64_t*>(rsp);
        const auto* limit =
            reinterpret_cast<const uint64_t*>(same_thread && sane ? t.stack_limit : rsp);
        int n = 0;
        for (const uint64_t* p = stack; p < limit && n < kMaxFrames; p++) {
            if (*p >= text && *p < text + kTextWindow)
                t.frames[n++] = *p - text;
        }
        t.frame_count = n;
        t.done.store(1, std::memory_order_release);
        return;
    }
}

void installDumpHandler() {
    static std::atomic<bool> installed{false};
    if (installed.exchange(true))
        return;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    void (*handler)(int, siginfo_t*, void*) = dumpHandler;
    memcpy(&action, &handler, sizeof(handler));
    action.sa_flags = kSaSiginfo | kSaRestart;
    sigemptyset(&action.sa_mask);
    sigaction(kSigDump, &action, nullptr);
}

void readIfMapped(uint64_t address, uint32_t (&words)[4]) {
    memset(words, 0, sizeof(words));
    void* start = nullptr;
    void* end = nullptr;
    uint32_t protection = 0;
    if (address < 0x10000 ||
        sceKernelQueryMemoryProtection(reinterpret_cast<void*>(address), &start, &end,
                                       &protection) != 0 ||
        !(protection & 1) || address + 16 > reinterpret_cast<uintptr_t>(end))
        return;
    memcpy(words, reinterpret_cast<const void*>(address), sizeof(words));
}

}  // namespace

extern "C" void ps4_watch_thread(const char* name) {
    installDumpHandler();
    const pthread_t self = pthread_self();
    const int tid = pthread_getthreadid_np();
    const int count = std::min(g_watched_count.load(), kMaxWatched);
    for (int i = 0; i < count; i++) {
        if (g_watched[i].thread.load() == self && g_watched[i].tid == tid)
            return;
    }
    const int index = g_watched_count.fetch_add(1);
    if (index >= kMaxWatched)
        return;
    snprintf(g_watched[index].name, sizeof(g_watched[index].name), "%s", name ? name : "?");
    g_watched[index].stack_limit = reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) + 16;
    g_watched[index].tid = tid;
    g_watched[index].thread.store(self);
}

// Names a watched thread later (guest threads get their names after they start).
extern "C" void ps4_name_watched_thread(pthread_t thread, const char* name) {
    const int count = std::min(g_watched_count.load(), kMaxWatched);
    for (int i = count - 1; i >= 0; i--) {
        if (g_watched[i].thread.load() == thread) {
            snprintf(g_watched[i].name, sizeof(g_watched[i].name), "%s", name ? name : "?");
            return;
        }
    }
}

// Signals one thread and waits (up to `wait_ms`) for its handler to record rip, stack and CPU time.
bool sampleThread(WatchedThread& t, int wait_ms) {
    t.done.store(0);
    if (pthread_kill(t.thread.load(), kSigDump) != 0)
        return false;  // exited
    const timespec pause = {0, 1000000};
    for (int w = 0; w < wait_ms && !t.done.load(std::memory_order_acquire); w++)
        nanosleep(&pause, nullptr);
    return t.done.load(std::memory_order_acquire) != 0;
}

// The busiest threads since the last call, as a share of one core (memory.log, every 5 s): which
// thread limits the frame rate.
extern "C" void ps4_thread_cpu_report(int fd) {
    static timespec last = {};
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const double window = double(now.tv_sec - last.tv_sec) + double(now.tv_nsec - last.tv_nsec) / 1e9;
    const bool first = last.tv_sec == 0;
    last = now;
    struct Busy {
        int index;
        uint64_t ns;
    } busy[kMaxWatched];
    int busy_count = 0;
    const int count = std::min(g_watched_count.load(std::memory_order_acquire), kMaxWatched);
    for (int i = 0; i < count; i++) {
        WatchedThread& t = g_watched[i];
        if (t.thread.load() == pthread_self() || !sampleThread(t, 20))
            continue;
        const uint64_t ns = t.cpu_ns - t.reported_cpu_ns;
        t.reported_cpu_ns = t.cpu_ns;
        if (!first && ns > 0)
            busy[busy_count++] = {i, ns};
    }
    if (first || window <= 0)
        return;
    std::sort(busy, busy + busy_count, [](const Busy& a, const Busy& b) { return a.ns > b.ns; });
    char line[640];
    int length = snprintf(line, sizeof(line), "  threads:");
    for (int i = 0; i < busy_count && i < 8 && length < 560; i++) {
        const double percent = double(busy[i].ns) / 1e9 / window * 100.0;
        if (percent < 2.0)
            break;
        length += snprintf(line + length, sizeof(line) - length, " %s %.0f%% |",
                           g_watched[busy[i].index].name, percent);
    }
    snprintf(line + length, sizeof(line) - length, "\n");
    writeLine(fd, line);
}

extern "C" void ps4_dump_thread_stacks(const char* path, const char* reason) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    char line[320];
    snprintf(line, sizeof(line), "==== %s (eboot addresses relative to .text)\n", reason);
    writeLine(fd, line);
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    const int count = std::min(g_watched_count.load(std::memory_order_acquire), kMaxWatched);
    for (int i = 0; i < count; i++) {
        WatchedThread& t = g_watched[i];
        if (t.thread.load() == pthread_self())
            continue;
        if (!sampleThread(t, 200)) {
            if (pthread_kill(t.thread.load(), 0) != 0)
                continue;  // exited
            snprintf(line, sizeof(line), "-- %s (thread id %d): no answer\n", t.name, t.tid);
            writeLine(fd, line);
            continue;
        }
        const bool in_eboot = t.rip >= text && t.rip < text + kTextWindow;
        snprintf(line, sizeof(line), "-- %s (thread id %d): cpu %.3f s, rip %s0x%llx\n", t.name,
                 t.tid, double(t.cpu_ns) / 1e9, in_eboot ? "elf " : "(system) ",
                 static_cast<unsigned long long>(in_eboot ? t.rip - text : t.rip));
        writeLine(fd, line);
        if (t.in_umtx) {
            uint32_t words[4], words2[4];
            readIfMapped(t.umtx_obj, words);
            readIfMapped(t.umtx_obj2, words2);
            snprintf(line, sizeof(line),
                     "   blocked in _umtx_op(obj %#llx, op %llu, val %#llx, obj2 %#llx); obj: %08x "
                     "%08x %08x %08x; obj2: %08x %08x %08x %08x\n",
                     static_cast<unsigned long long>(t.umtx_obj),
                     static_cast<unsigned long long>(t.umtx_op),
                     static_cast<unsigned long long>(t.umtx_val),
                     static_cast<unsigned long long>(t.umtx_obj2), words[0], words[1], words[2],
                     words[3], words2[0], words2[1], words2[2], words2[3]);
            writeLine(fd, line);
        }
        writeLine(fd, "   stack:");
        for (int f = 0; f < t.frame_count; f++) {
            snprintf(line, sizeof(line), " 0x%llx", static_cast<unsigned long long>(t.frames[f]));
            writeLine(fd, line);
        }
        writeLine(fd, "\n");
    }
    close(fd);
}

// ---------------------------------------------------------------------------------------------
// Activity counters (ps4_stat_add from the SDK), reported every 5 s in memory.log by the heartbeat:
// what the app waits for when nothing seems to happen (e.g. on the loading screen).
namespace {
constexpr int kStatCount = 15;
const char* const kStatNames[kStatCount] = {"sleep-req", "sleep-real", "read-bytes", "read-time",
                                            "wait-time", "poll-sleep", "frame-us", "frame-failed",
                                            "publish", "publish-failed", "presented",
                                            "not-presented", "dropped", "draws", "worker-idle"};
std::atomic<uint64_t> g_stat_count[kStatCount];
std::atomic<uint64_t> g_stat_sum[kStatCount];
std::atomic<uint64_t> g_stat_max[kStatCount];
uint64_t g_stat_last_count[kStatCount], g_stat_last_sum[kStatCount];
}  // namespace

extern "C" __attribute__((weak)) void ps4_heap_usage(size_t* main_bytes, size_t* overflow_bytes,
                                                     int* overflow_heaps);

extern "C" void ps4_stat_add(int id, uint64_t value) {
    if (id < 0 || id >= kStatCount)
        return;
    g_stat_count[id].fetch_add(1, std::memory_order_relaxed);
    g_stat_sum[id].fetch_add(value, std::memory_order_relaxed);
    uint64_t seen = g_stat_max[id].load(std::memory_order_relaxed);
    while (value > seen &&
           !g_stat_max[id].compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

// One line: per counter, events / sum / max since the last report (sums in the counter's unit:
// us for times, bytes for reads).
extern "C" void ps4_stat_report(int fd) {
    char line[512];
    int length = 0;
    for (int i = 0; i < kStatCount; i++) {
        if (!kStatNames[i][0])
            continue;
        const uint64_t count = g_stat_count[i].load(std::memory_order_relaxed);
        const uint64_t sum = g_stat_sum[i].load(std::memory_order_relaxed);
        const uint64_t max = g_stat_max[i].exchange(0, std::memory_order_relaxed);
        length += snprintf(line + length, sizeof(line) - length, "  %s %llu/%llu/%llu", kStatNames[i],
                           static_cast<unsigned long long>(count - g_stat_last_count[i]),
                           static_cast<unsigned long long>(sum - g_stat_last_sum[i]),
                           static_cast<unsigned long long>(max));
        g_stat_last_count[i] = count;
        g_stat_last_sum[i] = sum;
    }
    if (ps4_heap_usage) {
        size_t main_bytes = 0, overflow_bytes = 0;
        int overflow_heaps = 0;
        ps4_heap_usage(&main_bytes, &overflow_bytes, &overflow_heaps);
        length += snprintf(line + length, sizeof(line) - length, "  heap %zu+%zu MiB (%d overflow)",
                           main_bytes >> 20, overflow_bytes >> 20, overflow_heaps);
    }
    // The whole process's CPU time in this window (out of 5 s per core).
    static uint64_t last_cpu_ns = 0;
    timespec cpu = {};
    if (clock_gettime(kProcessCpuClock, &cpu) == 0) {
        const uint64_t cpu_ns = uint64_t(cpu.tv_sec) * 1000000000ull + uint64_t(cpu.tv_nsec);
        length += snprintf(line + length, sizeof(line) - length, "  cpu %.2f s",
                           double(cpu_ns - last_cpu_ns) / 1e9);
        last_cpu_ns = cpu_ns;
    }
    length += snprintf(line + length, sizeof(line) - length, "\n");
    writeLine(fd, line);
}

// ---------------------------------------------------------------------------------------------
// Every thread in the dumps (--wrap=pthread_create): std::thread, Mesa, SDL and the rest start
// through a trampoline that registers them, named after their start routine's ELF address
// (rex::thread threads are renamed with their guest names later).
namespace {
struct ThreadTrampoline {
    void* (*routine)(void*);
    void* argument;
};

// A ring of start records instead of the heap (threads start before the heap is set up): the new
// thread copies its record first thing, long before 1024 more threads are created.
ThreadTrampoline g_trampolines[1024];
std::atomic<uint32_t> g_trampoline_next{0};

void* threadTrampoline(void* data) {
    const ThreadTrampoline start = *static_cast<ThreadTrampoline*>(data);
    char name[48];
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    const uintptr_t routine = reinterpret_cast<uintptr_t>(start.routine);
    if (routine >= text && routine < text + kTextWindow)
        snprintf(name, sizeof(name), "pthread elf 0x%llx", static_cast<unsigned long long>(routine - text));
    else
        snprintf(name, sizeof(name), "pthread 0x%llx", static_cast<unsigned long long>(routine));
    ps4_watch_thread(name);
    return start.routine(start.argument);
}
}  // namespace

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*routine)(void*), void* argument);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*routine)(void*), void* argument) {
    ThreadTrampoline* start = &g_trampolines[g_trampoline_next.fetch_add(1) % 1024];
    start->routine = routine;
    start->argument = argument;
    return __real_pthread_create(thread, attr, threadTrampoline, start);
}
