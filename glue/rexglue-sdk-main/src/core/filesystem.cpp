/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>

#include <rex/filesystem.h>
#include <rex/platform.h>

namespace rex {
namespace filesystem {

std::filesystem::path GetResourcesFolder() {
#if REX_PLATFORM_PS4
  // The executable is /app0/eboot.bin, so the bundle layout below would point
  // outside the application mount.
  return "/app0/Resources";
#else
  return GetExecutableFolder().parent_path() / "Resources";
#endif
}

bool CreateParentFolder(const std::filesystem::path& path) {
  if (path.has_parent_path()) {
    auto parent_path = path.parent_path();
    if (!std::filesystem::exists(parent_path)) {
      return std::filesystem::create_directories(parent_path);
    }
  }
  return true;
}

}  // namespace filesystem
}  // namespace rex
