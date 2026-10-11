/**
 * @file        ps4/user_music_player_ps4.cpp
 * @brief       PS4 counterpart of the macOS user music player.
 *
 * The macOS player streams the user's music library through AVFoundation.
 * The PS4 build has no equivalent, so the player reports no tracks and every
 * request is declined, which makes the game fall back to its own radio.
 */

#include "input/user_music_player.h"

namespace gta4::input {

struct UserMusicPlayer::Impl {};

UserMusicPlayer::UserMusicPlayer(std::filesystem::path /*music_root*/) {}

UserMusicPlayer::~UserMusicPlayer() = default;

bool UserMusicPlayer::Next() {
  return false;
}

bool UserMusicPlayer::Previous() {
  return false;
}

bool UserMusicPlayer::Stop() {
  return false;
}

bool UserMusicPlayer::HasTracks() const {
  return false;
}

void PublishUserMusicPlayer(UserMusicPlayer* /*player*/) {}

bool IsUserMusicAvailable() {
  return false;
}

bool RequestUserMusicNext() {
  return false;
}

bool RequestUserMusicPrevious() {
  return false;
}

bool RequestUserMusicStop() {
  return false;
}

}  // namespace gta4::input
