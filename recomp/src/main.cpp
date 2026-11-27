// KAMO game host: runs the statically recompiled Kirby & The Amazing Mirror (USA) on the
// GBARecomp runtime. The generated cartridge code is linked in at build time; the player
// supplies the ROM and a retail GBA BIOS at launch (neither is distributed).
//
// Online play uses GBARecomp's whole-session netplay: every peer runs all linked consoles
// and only input crosses the network (delay-sync or rollback). Up to four players.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

#include "kamo_game.h"
#include "player_config.h"
#include "runtime.h"
#if defined(RECOMP_LAUNCHER)
#include "launcher_seam.h"
#endif

namespace {

// The newest paired archive the netplay runner wrote to <player dir>/netplay/.
std::filesystem::path newest_online_save(const std::filesystem::path& player_dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(player_dir / "netplay", ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".paired") continue;
        const auto time = entry.last_write_time(ec);
        if (!ec && (newest.empty() || time > newest_time)) {
            newest = entry.path();
            newest_time = time;
        }
    }
    return newest;
}

}  // namespace

int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = kamo::kGameName;
    options.builtin_rom_sha1 = kamo::kRomSha1;
    options.launcher_region = "USA";

    auto netplay = kamo::make_netplay_launch();
    std::vector<std::string> args(argv, argv + argc);
    // --continue-online: resume the newest saved online match. Every player in the match
    // starts KAMO this way, then the lobby (or direct IP) works as usual.
    for (auto it = args.begin() + 1; it != args.end(); ++it) {
        if (*it != "--continue-online") continue;
        args.erase(it);
        const auto saved = newest_online_save(gbarecomp::player_config_directory(options, argv[0]));
        if (saved.empty()) {
            std::fprintf(stderr, "KAMO: no saved online match in netplay/; starting a new one\n");
        } else {
            std::fprintf(stderr, "KAMO: continuing online match %s\n", saved.string().c_str());
            args.push_back("--netplay-resume");
            args.push_back(saved.string());
        }
        break;
    }
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
