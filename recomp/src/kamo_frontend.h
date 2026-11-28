#pragma once
// KAMO's own menu window, shown when KAMO.exe starts: first-run ROM setup, then
// Play alone / Play online / Options, plus automatic updates.

#include <filesystem>
#include <string>
#include <vector>

#include "runtime.h"

namespace kamo {

// Runs the menu. Returns true to start the game: `args` then carries the ROM and the player's
// settings, and for an online match `options.netplay` holds the room's launch. False quits.
bool run_frontend(std::vector<std::string>& args, gbarecomp::RunOptions& options,
                  const std::filesystem::path& dir);

}  // namespace kamo
