#include "font_atlas.h"
#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <xxhash.h>

namespace rex::graphics::gta4_native::core {
const char* VectorFontSetName(VectorFontSet set) {
  switch (set) {
    case VectorFontSet::kGta4: return "gta4";
    case VectorFontSet::kTlad: return "tlad";
    case VectorFontSet::kTbogt: return "tbogt";
  }
  return "unknown";
}
VectorFontSet SelectVectorFontSet(size_t index, uint64_t hash) {
  if (index == 1 && hash == 0xD319ABCEFD23508Dull) return VectorFontSet::kTbogt;
  if (index == 2 && hash == 0x57551F2730323DF3ull) return VectorFontSet::kTlad;
  if (index == 2 && hash == 0x6C569517F27F53D2ull) return VectorFontSet::kTbogt;
  return VectorFontSet::kGta4;
}
uint64_t StockFontIdentity(std::span<const uint8_t> bytes) {
  if (bytes.size() < kFontIdentityBytes) return 0;
  std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), XXH3_freeState);
  if (!state) return 0;
  const std::array<uint8_t, 16> prefix{};
  XXH3_64bits_reset(state.get());
  XXH3_64bits_update(state.get(), prefix.data(), prefix.size());
  XXH3_64bits_update(state.get(), bytes.data(), kFontIdentityBytes - prefix.size());
  return XXH3_64bits_digest(state.get());
}
const VectorFontAtlas* FindVectorFontAtlas(VectorFontSet set, size_t atlas_index) {
  static constexpr const char* files[3][3] = {
      {"font1.png", "font2.png", "font3.png"},
      {"tlad/font1.png", "tlad/font2.png", "tlad/font3.png"},
      {"tbogt/font1.png", "tbogt/font2.png", "tbogt/font3.png"}};
  static std::array<std::array<VectorFontAtlas, 3>, 3> atlases{};
  static std::array<std::array<std::once_flag, 3>, 3> loads{};
  const auto index = size_t(set);
  if (index >= atlases.size() || atlas_index >= atlases[index].size()) return nullptr;
  auto& atlas = atlases[index][atlas_index];
  std::call_once(loads[index][atlas_index], [&] {
    atlas.filename = files[index][atlas_index];
    auto path = rex::filesystem::GetResourcesFolder() / "font_atlases" / atlas.filename;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
#ifdef GTA4_NATIVE_FONT_ASSET_ROOT
      path = std::filesystem::path(GTA4_NATIVE_FONT_ASSET_ROOT) / atlas.filename;
#endif
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream ? stream.tellg() : std::streampos(-1);
    if (size <= 0 || uint64_t(size) > 32u * 1024u * 1024u) {
      REXLOG_WARN("native-fonts: unavailable atlas {}; retaining stock texture", path.string());
      return;
    }
    std::vector<uint8_t> encoded(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(encoded.data()), encoded.size())) return;
    int width = 0, height = 0;
    const auto rgba = rex::ui::DecodeImageRGBA(encoded.data(), encoded.size(), width, height);
    if (width != int(kFontAtlasExtent) || height != int(kFontAtlasExtent) ||
        rgba.size() != size_t(kFontAtlasExtent) * kFontAtlasExtent * 4) {
      REXLOG_WARN("native-fonts: invalid atlas {}; retaining stock texture", path.string());
      return;
    }
    atlas.alpha.resize(size_t(width) * height);
    for (size_t i = 0; i < atlas.alpha.size(); ++i) atlas.alpha[i] = rgba[i * 4 + 3];
    REXLOG_INFO("native-fonts: loaded atlas={} extent={} alpha-hash={:016X}",
                atlas.filename, kFontAtlasExtent, XXH3_64bits(atlas.alpha.data(), atlas.alpha.size()));
  });
  return atlas.alpha.empty() ? nullptr : &atlas;
}
std::vector<std::vector<uint8_t>> FontCoverageMips(std::span<const uint8_t> alpha,
                                                 uint32_t extent, uint32_t levels) {
  if (!extent || !std::has_single_bit(extent) || extent > kFontAtlasExtent ||
      !levels || levels > std::bit_width(extent) || alpha.size() != size_t(extent) * extent) return {};
  std::vector<std::vector<uint8_t>> result;
  result.reserve(levels);
  result.emplace_back(alpha.begin(), alpha.end());
  for (uint32_t level = 1; level < levels; ++level) {
    const auto& input = result.back();
    const uint32_t next = extent >> 1;
    std::vector<uint8_t> output(size_t(next) * next);
    for (uint32_t y = 0; y < next; ++y) for (uint32_t x = 0; x < next; ++x) {
      const size_t a = size_t(y * 2) * extent + x * 2;
      const uint32_t sum = uint32_t(input[a]) + input[a + 1] + input[a + extent] + input[a + extent + 1];
      output[size_t(y) * next + x] = uint8_t((sum + 2) / 4);
    }
    result.emplace_back(std::move(output)); extent = next;
  }
  return result;
}
}  // namespace rex::graphics::gta4_native::core
