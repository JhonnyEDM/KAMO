#include "kamo_game.h"

#include "gba_netplay_build_identity.h"

namespace kamo {

std::shared_ptr<gbarecomp::GbaNetplayLaunch> make_netplay_launch() {
    auto launch = gbarecomp::make_gba_netplay_launch(kProgramId, GBARECOMP_NETPLAY_BUILD_ID, kRomSha256);
    launch->max_players = 4;  // the game links up to four consoles on one cable
    return launch;
}

}  // namespace kamo
