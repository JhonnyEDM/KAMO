#pragma once
// Player settings kept by KAMO's menu in kamo.ini next to KAMO.exe.

#include <filesystem>
#include <string>

namespace kamo {

// A path as UTF-8 text (C++20 u8string is char8_t).
inline std::string utf8(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

struct Settings {
    std::string name;          // online display name
    int scale = 3;             // window size, times the 240x160 screen
    bool fullscreen = false;
    int volume = 100;          // 0..100
    int input_delay = 0;       // online input delay in frames; 0 = automatic from the ping
    std::string kirby_color;   // online: this player's Kirby colour, "RRGGBB"; empty = original
    bool show_names = true;    // online: players' names above their Kirbys
};

Settings load_settings(const std::filesystem::path& dir);
void save_settings(const std::filesystem::path& dir, const Settings& settings);

// rom.cfg: the path of the player's ROM, shared with the runtime.
std::filesystem::path load_rom_path(const std::filesystem::path& dir);
void save_rom_path(const std::filesystem::path& dir, const std::filesystem::path& rom);

// Empty when `rom` is the supported dump, otherwise the reason it is not.
std::string check_rom(const std::filesystem::path& rom);

// Keyboard keys of the GBA buttons, kept in keybinds.ini [player1] (SDL key names).
struct KeyBinding {
    const char* id;     // keybinds.ini key
    const char* label;  // button name shown in the menu
    std::string key;    // SDL key name
};
inline constexpr int kButtonCount = 10;
void load_keys(const std::filesystem::path& dir, KeyBinding (&keys)[kButtonCount]);
void save_keys(const std::filesystem::path& dir, const KeyBinding (&keys)[kButtonCount]);

// The newest saved online match in <dir>/netplay, or empty.
std::filesystem::path newest_online_save(const std::filesystem::path& dir);

}  // namespace kamo
