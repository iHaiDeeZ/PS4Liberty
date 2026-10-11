/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 *
 * PS4 (OpenOrbis) variant of exception_handler_posix.cpp. The PS4 kernel is
 * FreeBSD-derived and delivers a FreeBSD/amd64 ucontext to signal handlers
 * (struct __mcontext with mc_* register fields and an fxsave image in
 * mc_fpstate), not the Linux gregs[]/fpregs layout. The build must therefore
 * see FreeBSD-layout <signal.h> definitions (see PS4_LIBC_OVERLAY in
 * toolchains/ps4-openorbis.cmake); the static_assert below enforces that.
 */

#include <rex/exception_handler.h>

#if REX_PLATFORM_PS4

#include <signal.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>

#include <ucontext.h>

#ifdef REG_RIP
#error "exception_handler_ps4.cpp needs the FreeBSD mcontext layout the PS4 kernel uses, but the Linux/musl gregs layout is in effect. Configure with PS4_LIBC_OVERLAY."
#endif

namespace rex::arch {

namespace {

// Register slot type of the FreeBSD mcontext (a long on amd64).
using McRegister = decltype(mcontext_t::mc_rip);

// FreeBSD <machine/ucontext.h>: mc_fpstate holds an fxsave image when
// mc_fpformat is _MC_FPFMT_XMM, and the state is only meaningful if the
// thread had used the FPU (mc_ownedfp != _MC_FPOWNED_NONE).
constexpr long kMcFpFmtXmm = 0x10002;
constexpr long kMcFpOwnedNone = 0x20000;
// Byte offset of XMM0 within the 512-byte fxsave area.
constexpr size_t kFxsaveXmmOffset = 160;

// x86 page fault error code: bit 1 set when the faulting access was a write.
constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;

bool HasXmmState(const mcontext_t& mcontext) {
  return mcontext.mc_fpformat == kMcFpFmtXmm && mcontext.mc_ownedfp != kMcFpOwnedNone;
}

uint8_t* XmmState(mcontext_t& mcontext) {
  return reinterpret_cast<uint8_t*>(mcontext.mc_fpstate) + kFxsaveXmmOffset;
}

// The order must match the order in X64Register (and HostThreadContext's
// int_registers): rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8-r15.
McRegister* IntRegister(mcontext_t& mcontext, uint32_t index) {
  switch (index) {
    case 0: return &mcontext.mc_rax;
    case 1: return &mcontext.mc_rcx;
    case 2: return &mcontext.mc_rdx;
    case 3: return &mcontext.mc_rbx;
    case 4: return &mcontext.mc_rsp;
    case 5: return &mcontext.mc_rbp;
    case 6: return &mcontext.mc_rsi;
    case 7: return &mcontext.mc_rdi;
    case 8: return &mcontext.mc_r8;
    case 9: return &mcontext.mc_r9;
    case 10: return &mcontext.mc_r10;
    case 11: return &mcontext.mc_r11;
    case 12: return &mcontext.mc_r12;
    case 13: return &mcontext.mc_r13;
    case 14: return &mcontext.mc_r14;
    case 15: return &mcontext.mc_r15;
    default: return nullptr;
  }
}

}  // namespace

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;
struct sigaction original_sigbus_handler_;

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  mcontext_t& mcontext = reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;

  HostThreadContext thread_context;

  thread_context.rip = uint64_t(mcontext.mc_rip);
  thread_context.eflags = uint32_t(mcontext.mc_rflags);
  for (uint32_t i = 0; i < 16; ++i) {
    thread_context.int_registers[i] = uint64_t(*IntRegister(mcontext, i));
  }
  if (HasXmmState(mcontext)) {
    std::memcpy(thread_context.xmm_registers, XmmState(mcontext),
                sizeof(thread_context.xmm_registers));
  } else {
    std::memset(thread_context.xmm_registers, 0, sizeof(thread_context.xmm_registers));
  }

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    // FreeBSD may report a protection fault on a mapped page as SIGBUS
    // rather than SIGSEGV; both carry the same page fault error code.
    case SIGSEGV:
    case SIGBUS: {
      Exception::AccessViolationOperation access_violation_operation =
          (uint64_t(mcontext.mc_err) & kX86PageFaultErrorCodeWrite)
              ? Exception::AccessViolationOperation::kWrite
              : Exception::AccessViolationOperation::kRead;
      uint64_t fault_address = reinterpret_cast<uint64_t>(signal_info->si_addr);
      if (!fault_address) {
        fault_address = uint64_t(mcontext.mc_addr);
      }
      ex.InitializeAccessViolation(&thread_context, fault_address, access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

  for (size_t i = 0; i < rex::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
      mcontext.mc_rip = McRegister(thread_context.rip);
      mcontext.mc_rflags = McRegister(thread_context.eflags);
      uint32_t modified_register_index;
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining, &modified_register_index)) {
        modified_int_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        *IntRegister(mcontext, modified_register_index) =
            McRegister(thread_context.int_registers[modified_register_index]);
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      if (modified_xmm_registers_remaining && !HasXmmState(mcontext)) {
        REXLOG_ERROR("Exception handler modified XMM registers, but the signal context has no "
                     "fxsave state to write them back to");
        modified_xmm_registers_remaining = 0;
      }
      while (rex::bit_scan_forward(modified_xmm_registers_remaining, &modified_register_index)) {
        modified_xmm_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        std::memcpy(XmmState(mcontext) + modified_register_index * sizeof(vec128_t),
                    &thread_context.xmm_registers[modified_register_index], sizeof(vec128_t));
      }
      return;
    }
  }

  // Not a guest access: hand the fault to whoever had the signal before (the
  // runtime crash logger), or let it kill the process. Returning without that
  // would re-run the faulting instruction forever.
  const struct sigaction& original = signal_number == SIGILL    ? original_sigill_handler_
                                     : signal_number == SIGSEGV ? original_sigsegv_handler_
                                                                : original_sigbus_handler_;
  if ((original.sa_flags & SA_SIGINFO) && original.sa_sigaction) {
    original.sa_sigaction(signal_number, signal_info, signal_context);
    return;
  }
  if (!(original.sa_flags & SA_SIGINFO) && original.sa_handler != SIG_DFL &&
      original.sa_handler != SIG_IGN) {
    original.sa_handler(signal_number);
    return;
  }
  signal(signal_number, SIG_DFL);
}

void ExceptionHandler::Install(Handler fn, void* data) {
  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    // SA_ONSTACK: on the thread's alternate signal stack (if any; see
    // ps4_install_signal_stack), so a stack overflow still gets reported.
    signal_handler.sa_flags = SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
    if (sigaction(SIGBUS, &signal_handler, &original_sigbus_handler_) != 0) {
      assert_always("Failed to install new SIGBUS handler");
    }
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < rex::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
      if (sigaction(SIGBUS, &original_sigbus_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGBUS handler");
      }
      signal_handlers_installed_ = false;
    }
  }
}

}  // namespace rex::arch

#endif  // REX_PLATFORM_PS4
