#pragma once
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <rex/diagnostics/policy.h>
#include <rex/platform.h>
#include <rex/graphics/gta4_native/gpu_pass_origin.h>
#include <rex/graphics/gta4_native/title_commands.h>
#include "../../src/graphics/gta4_metal/temporal/title_boundary.h"

namespace gta4::gpu_pass {
using rex::graphics::gta4_native::GpuPassOrigin;
inline thread_local GpuPassOrigin current{};
inline thread_local bool final_composite = false;
class ScopedFinalComposite {
 public:
  explicit ScopedFinalComposite(bool final) : previous_(final_composite) {
    final_composite = final;
  }
  ~ScopedFinalComposite() { final_composite = previous_; }
  ScopedFinalComposite(const ScopedFinalComposite&) = delete;
  ScopedFinalComposite& operator=(const ScopedFinalComposite&) = delete;
 private:
  bool previous_;
};
inline std::atomic<bool> temporal_inputs{false};
inline std::atomic<uint64_t> next_scope{1},unresolved_scopes{0},conflicting_scopes{0},oversized_envelopes{0};
inline bool Enabled(){
#if REX_PLATFORM_PS4
  // The shadow / reflection update intervals (graphics_system.cpp, PS4 performance mode) find
  // those passes by these markers; without them nothing is ever skipped.
  return true;
#endif
  static const bool markers=[] {const char* p=std::getenv("REX_GTA4_GPU_PASS_MARKERS");return p&&std::strcmp(p,"1")==0;}();
  return temporal_inputs.load(std::memory_order_relaxed)||markers||rex::diagnostics::IsEnabled(rex::diagnostics::Category::kNativeProfiler);
}
class ScopedExecutedList {
 public:
  template <typename Read>
  ScopedExecutedList(uint32_t arena,uint32_t offset,uint32_t end,uint32_t ordinal,uint32_t r13,Read&& read)
      :active_(Enabled()) {
    if(!active_)return;
    previous_=current;
    current=rex::graphics::gta4_native::DecodeExecutedGpuDrawList(arena,offset,end,ordinal,r13,std::forward<Read>(read));
    current.scope=next_scope.fetch_add(1,std::memory_order_relaxed);
    if(!current.attributed())++unresolved_scopes;
    if(current.source==rex::graphics::gta4_native::GpuPassOriginSource::kMismatch)++conflicting_scopes;
  }
  ~ScopedExecutedList(){if(active_)current=previous_;}
  ScopedExecutedList(const ScopedExecutedList&)=delete;
  ScopedExecutedList& operator=(const ScopedExecutedList&)=delete;
 private:GpuPassOrigin previous_{};bool active_=false;
};
// Wrap at the final submission boundary, including replay and other diagnostic
// envelopes. The renderer owns a POD copy before this stack storage expires.
template <typename Graphics>
bool Submit(Graphics* graphics,uint32_t abi,const void* command,size_t size){
  using namespace rex::graphics::gta4_native;
  if(!graphics)return false;
  if(!Enabled())return graphics->SubmitTitleCommand(kTitleId,abi,command,size);
  std::array<uint8_t,4096> storage;
  const size_t packed=PackGpuPassEnvelope(storage,command,size,abi,current);
  if(!packed){++oversized_envelopes;return graphics->SubmitTitleCommand(kTitleId,abi,command,size);}
  return graphics->SubmitTitleCommand(kTitleId,kGpuPassEnvelopeAbi,storage.data(),packed);
}
} // namespace gta4::gpu_pass
