/**
 * @file        ps4/microphone_permission_ps4.cpp
 * @brief       PS4 counterpart of the macOS microphone permission prompt.
 *
 * The PS4 has no per-app microphone permission, and voice capture is not
 * implemented for the PS4 build, so capture is reported as unavailable.
 */

#include "network/gta4_microphone_permission.h"

namespace gta4::voice {

MicrophonePermissionStatus GetMicrophonePermissionStatus() noexcept {
  return MicrophonePermissionStatus::kRestricted;
}

void RequestMicrophonePermission(std::function<void()> completion) {
  if (completion) {
    completion();
  }
}

}  // namespace gta4::voice
