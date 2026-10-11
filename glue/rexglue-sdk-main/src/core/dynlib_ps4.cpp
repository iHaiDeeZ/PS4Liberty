#include <rex/platform.h>

#if REX_PLATFORM_PS4

#include <rex/logging/macros.h>
#include <rex/platform/dynlib.h>

#include <cstdint>

// OpenOrbis libkernel module + dlsym APIs. Declared here to avoid forcing the
// whole SDK's <orbis/libkernel.h> on every consumer of dynlib.h.
//
// NOTE: The OpenOrbis <orbis/libkernel.h> header incorrectly declares
// sceKernelLoadStartModule as returning uint32_t, but the real SCE kernel
// ABI returns a signed SceKernelModule (int32_t) — negative values are
// error codes (0x80000000-range), and OpenOrbis samples uniformly assign
// the result into a signed int. We match the true ABI here so the sign
// check below is defined behavior instead of an unsigned-to-signed cast.
extern "C" {
std::int32_t sceKernelLoadStartModule(const char*, std::size_t, const void*,
                                      std::uint32_t, void*, void*);
std::int32_t sceKernelStopUnloadModule(std::int32_t, std::size_t, const void*,
                                       std::uint32_t, void*, std::int32_t*);
std::int32_t sceKernelDlsym(std::int32_t handle, const char* symbol, void** address);
}

namespace rex::platform {

namespace {
// Handle encoding for PS4. handle_ is an opaque void* (dlopen-style); a PS4
// module is identified by an int32 module ID instead, so the ID is packed
// into the pointer bits:
//
//   bit  63                                    32 31                    0
//       +---------------------------------------+------------------------+
//       | kLoadedFlag (1 ULL << 32 when loaded) | module_id (int32 bits) |
//       +---------------------------------------+------------------------+
//
// A null handle_ means "not loaded". The kLoadedFlag bit disambiguates a
// successful load that returned module_id == 0 from null. Module IDs are
// stored in the low 32 bits as their raw two's-complement int32
// representation, so Decode() round-trips back to int32_t losslessly.
constexpr std::uintptr_t kLoadedFlag = static_cast<std::uintptr_t>(1) << 32;

inline void* Encode(std::int32_t module_id) {
  return reinterpret_cast<void*>(kLoadedFlag | static_cast<std::uint32_t>(module_id));
}
inline std::int32_t Decode(void* h) {
  return static_cast<std::int32_t>(
      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(h) & 0xFFFFFFFFu));
}
}  // namespace

DynamicLibrary::~DynamicLibrary() { Close(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

// sceKernelLoadStartModule always resolves a module's imports at load time,
// so both SymbolResolution modes behave like kImmediate.
bool DynamicLibrary::Load(const std::filesystem::path& path, SymbolResolution /*mode*/) {
  Close();
  if (path.empty()) {
    return false;
  }
  const std::string p = path.string();
  std::int32_t result = 0;
  const std::int32_t module_id =
      sceKernelLoadStartModule(p.c_str(), 0, nullptr, 0, nullptr, &result);
  // sceKernelLoadStartModule returns a negative SCE error on failure; the
  // high bit (0x80000000) is set on error codes.
  if (module_id < 0) {
    REXLOG_WARN("sceKernelLoadStartModule('{}') failed: 0x{:08x}", p,
                static_cast<std::uint32_t>(module_id));
    return false;
  }
  handle_ = Encode(module_id);
  return true;
}

void DynamicLibrary::Close() {
  if (!handle_) {
    return;
  }
  const std::int32_t module_id = Decode(handle_);
  std::int32_t stop_result = 0;
  const std::int32_t rc = sceKernelStopUnloadModule(
      module_id, 0, nullptr, 0, nullptr, &stop_result);
  if (rc < 0) {
    REXLOG_WARN("sceKernelStopUnloadModule({}) failed: 0x{:08x}", module_id,
                static_cast<std::uint32_t>(rc));
  }
  handle_ = nullptr;
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  if (!handle_ || !name) {
    return nullptr;
  }
  void* addr = nullptr;
  const std::int32_t rc = sceKernelDlsym(Decode(handle_), name, &addr);
  if (rc < 0) {
    return nullptr;
  }
  return addr;
}

}  // namespace rex::platform

#endif  // REX_PLATFORM_PS4
