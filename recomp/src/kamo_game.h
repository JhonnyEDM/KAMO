#pragma once
// Game identity and netplay capabilities shared by the KAMO executable and its test tools.

#include <memory>

#include "multiplayer_launch.h"

namespace kamo {

inline constexpr const char* kGameName = "Kirby & The Amazing Mirror";
inline constexpr const char* kProgramId = "kamo-b8ke";
// Supported dump: Kirby & The Amazing Mirror (USA), game code B8KE.
inline constexpr const char* kRomSha1 = "274b102b6d940f46861a92b4e65f89a51815c12c";
inline constexpr const char* kRomSha256 = "cfb194cd0cb373787094a962852e3e564fb2ecdb0609fe065afdd8de3f4e11f1";

// Netplay description of this build: identity, up to four consoles on one cable.
std::shared_ptr<gbarecomp::GbaNetplayLaunch> make_netplay_launch();

}  // namespace kamo
