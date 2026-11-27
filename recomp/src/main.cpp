// KAMO game host: runs the statically recompiled Kirby & The Amazing Mirror (USA) on the
// GBARecomp runtime. The generated cartridge code is linked in at build time; the player
// supplies the ROM and a retail GBA BIOS at launch (neither is distributed).
//
// Online play uses GBARecomp's whole-session netplay: every peer runs all linked consoles
// and only input crosses the network (delay-sync or rollback). Up to four players.

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "kamo_game.h"
#include "runtime.h"
#if defined(RECOMP_LAUNCHER)
#include "launcher_seam.h"
#endif

int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = kamo::kGameName;
    options.builtin_rom_sha1 = kamo::kRomSha1;
    options.launcher_region = "USA";

    auto netplay = kamo::make_netplay_launch();
    std::vector<std::string> args(argv, argv + argc);
    try {
        gbarecomp::parse_gba_netplay_arguments(args, *netplay);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "KAMO: %s\n", e.what());
        return 2;
    }
    options.netplay = netplay;

#if defined(RECOMP_LAUNCHER)
    // The launcher window: pick ROM and BIOS, change settings, or open the online lobby.
    // It is skipped for command-line netplay and headless runs, and returns non-zero
    // when the player closes it.
    if (gbarecomp_launcher_preboot(args, options)) return 0;
#endif

    std::vector<char*> kept;
    kept.reserve(args.size() + 1);
    for (auto& a : args) kept.push_back(a.data());
    kept.push_back(nullptr);
    return gbarecomp::run_game(int(args.size()), kept.data(), options);
}
