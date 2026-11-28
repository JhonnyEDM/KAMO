// KAMO game host: runs the statically recompiled Kirby & The Amazing Mirror (USA) on the
// GBARecomp runtime. The generated cartridge code is linked in at build time; the player
// supplies the ROM (not distributed). No BIOS is needed: the runtime boots the cartridge
// directly and services the BIOS calls itself.
//
// Online play uses GBARecomp's whole-session netplay: every peer runs all linked consoles
// and only input crosses the network (delay-sync or rollback). Up to four players.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "kamo_frontend.h"
#include "kamo_game.h"
#include "kamo_settings.h"
#include "kamo_update.h"
#include "player_config.h"
#include "runtime.h"

namespace {

// Exit code of a menu child whose game ended: the parent opens the menu again.
constexpr int kBackToMenu = 42;
constexpr const char* kMenuChild = "--menu-child";

// Removes `flag` and its value from args; returns the value, or empty.
std::string take_value(std::vector<std::string>& args, const char* flag) {
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] != flag) continue;
        std::string value = args[i + 1];
        args.erase(args.begin() + i, args.begin() + i + 2);
        return value;
    }
    return {};
}

bool take_flag(std::vector<std::string>& args, const char* flag) {
    const auto before = args.size();
    std::erase(args, std::string(flag));
    return args.size() != before;
}

// Command-line runs (tests, tools, explicit ROM) skip the menu.
bool wants_menu(const std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--no-menu" || a == "--rom" || a == "--frames" || a == "--steps" || a == "--no-window" ||
            a == "--tcp" || a == "--load-state" || a == "--help" || a == "-h" || a.rfind("--dump", 0) == 0)
            return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    gbarecomp::RunOptions options;
    options.builtin_game_name = "KAMO";
    options.builtin_rom_sha1 = kamo::kRomSha1;
    options.launcher_region = "USA";
    const auto dir = gbarecomp::player_config_directory(options, argv[0]);

    auto netplay = kamo::make_netplay_launch();
    std::vector<std::string> args(argv, argv + argc);
    const bool menu_child = take_flag(args, kMenuChild);
    // --use-rom <file>: RecompLauncher hands over the player's ROM; KAMO keeps it and opens its menu.
    const std::string handed_rom = take_value(args, "--use-rom");
    if (!handed_rom.empty() && kamo::check_rom(std::filesystem::u8path(handed_rom)).empty())
        kamo::save_rom_path(dir, std::filesystem::u8path(handed_rom));
    // --continue-online: resume the newest saved online match (command-line netplay).
    for (auto it = args.begin() + 1; it != args.end(); ++it) {
        if (*it != "--continue-online") continue;
        args.erase(it);
        const auto saved = kamo::newest_online_save(dir);
        if (saved.empty()) {
            std::fprintf(stderr, "KAMO: no saved online match in netplay/; starting a new one\n");
        } else {
            std::fprintf(stderr, "KAMO: continuing online match %s\n", saved.string().c_str());
            args.push_back("--netplay-resume");
            args.push_back(saved.string());
        }
        break;
    }
    const bool menu = wants_menu(args);
    std::erase(args, std::string("--no-menu"));
    try {
        gbarecomp::parse_gba_netplay_arguments(args, *netplay);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "KAMO: %s\n", e.what());
        return 2;
    }
    options.netplay = netplay;

    if (menu && !netplay->enabled && !kamo::run_frontend(args, options, dir)) return 0;

#if !defined(GBARECOMP_HAVE_BIOS_RECOMP)
    // No BIOS code is linked: always boot without a BIOS image, even if one is named.
    for (std::size_t i = 1; i < args.size();) {
        if (args[i] == "--bios" && i + 1 < args.size()) args.erase(args.begin() + i, args.begin() + i + 2);
        else ++i;
    }
    args.push_back("--bios");
    args.emplace_back();
#endif

    std::vector<char*> kept;
    kept.reserve(args.size() + 1);
    for (auto& a : args) kept.push_back(a.data());
    kept.push_back(nullptr);
    const int result = gbarecomp::run_game(int(args.size()), kept.data(), options);
    if (!menu) return result;
    // Leaving the game returns to KAMO's menu. The menu runs in a fresh process (the game
    // runtime is not built to start twice in one process); this first process waits, so a
    // launcher keeps tracking KAMO, and starts the menu again each time a game ends.
    if (menu_child) return kBackToMenu;
    for (;;) {
        const int code = kamo::run_self_and_wait(kMenuChild);
        if (code != kBackToMenu) return code < 0 ? result : code;
    }
}
