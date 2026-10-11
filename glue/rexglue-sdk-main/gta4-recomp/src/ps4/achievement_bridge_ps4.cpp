/**
 * @file        ps4/achievement_bridge_ps4.cpp
 * @brief       PS4 counterpart of the Game Center achievement bridge.
 *
 * Achievements are still tracked by the runtime's achievement manager and
 * shown by its overlay; this bridge only mirrors unlocks to the platform
 * service. Homebrew trophy support needs a trophy pack and NP registration in
 * the PKG, which the PS4 build does not ship yet, so unlocks are logged.
 */

#include "achievement_bridge_gc.h"

#include <rex/logging.h>

namespace gta4::game_center {

void Initialize() {
  REXLOG_INFO("PS4: platform trophies are not wired up; achievements stay in-game only");
}

void SubmitAchievement(uint32_t xbox_id) {
  REXLOG_INFO("PS4: achievement {} unlocked (not mirrored to PS4 trophies)", xbox_id);
}

}  // namespace gta4::game_center
