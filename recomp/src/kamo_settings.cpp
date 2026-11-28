#include "kamo_settings.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "kamo_game.h"
#include "sha1.h"

namespace kamo {
namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

int to_int(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

}  // namespace

Settings load_settings(const std::filesystem::path& dir) {
    Settings s;
    std::ifstream in(dir / "kamo.ini");
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "name") s.name = value.substr(0, 24);
        else if (key == "scale") s.scale = std::clamp(to_int(value, 3), 1, 8);
        else if (key == "fullscreen") s.fullscreen = value == "1";
        else if (key == "volume") s.volume = std::clamp(to_int(value, 100), 0, 100);
        else if (key == "input_delay") s.input_delay = std::clamp(to_int(value, 0), 0, 10);
    }
    return s;
}

void save_settings(const std::filesystem::path& dir, const Settings& s) {
    std::ofstream out(dir / "kamo.ini", std::ios::trunc);
    out << "[kamo]\n"
        << "name = " << s.name << "\n"
        << "scale = " << s.scale << "\n"
        << "fullscreen = " << (s.fullscreen ? 1 : 0) << "\n"
        << "volume = " << s.volume << "\n"
        << "input_delay = " << s.input_delay << "\n";
}

std::filesystem::path load_rom_path(const std::filesystem::path& dir) {
    std::ifstream in(dir / "rom.cfg");
    std::string line;
    std::getline(in, line);
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // the installer writes a BOM
    line = trim(line);
    return line.empty() ? std::filesystem::path{} : std::filesystem::u8path(line);
}

void save_rom_path(const std::filesystem::path& dir, const std::filesystem::path& rom) {
    std::ofstream out(dir / "rom.cfg", std::ios::trunc);
    out << utf8(rom) << "\n";
}

std::string check_rom(const std::filesystem::path& rom) {
    std::error_code ec;
    if (rom.empty() || !std::filesystem::is_regular_file(rom, ec)) return "not-found";
    const auto size = std::filesystem::file_size(rom, ec);
    if (ec || size == 0 || size > 32u * 1024 * 1024) return "wrong-rom";
    std::ifstream in(rom, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        return "not-found";
    return gba::sha1(bytes.data(), bytes.size()).hex() == kRomSha1 ? std::string{} : "wrong-rom";
}

void load_keys(const std::filesystem::path& dir, KeyBinding (&keys)[kButtonCount]) {
    static const KeyBinding defaults[kButtonCount] = {
        {"a", "A", "X"},           {"b", "B", "Z"},
        {"l", "L", "C"},           {"r", "R", "V"},
        {"start", "Start", "Return"}, {"select", "Select", "Right Shift"},
        {"up", "Up", "Up"},        {"down", "Down", "Down"},
        {"left", "Left", "Left"},  {"right", "Right", "Right"},
    };
    std::copy(std::begin(defaults), std::end(defaults), std::begin(keys));
    std::ifstream in(dir / "keybinds.ini");
    std::string line;
    bool player1 = false;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { player1 = line == "[player1]"; continue; }
        const auto eq = line.find('=');
        if (!player1 || eq == std::string::npos) continue;
        const auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        for (auto& k : keys)
            if (key == k.id && !value.empty()) k.key = value;
    }
}

void save_keys(const std::filesystem::path& dir, const KeyBinding (&keys)[kButtonCount]) {
    // Rewrite only the [player1] lines this menu owns; keep everything else.
    std::vector<std::string> lines;
    {
        std::ifstream in(dir / "keybinds.ini");
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    bool written[kButtonCount] = {};
    bool player1 = false;
    std::size_t player1_end = lines.size();
    bool has_player1 = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (player1) player1_end = i;
            player1 = t == "[player1]";
            has_player1 |= player1;
            continue;
        }
        const auto eq = t.find('=');
        if (!player1 || t.empty() || t[0] == '#' || eq == std::string::npos) continue;
        const auto key = trim(t.substr(0, eq));
        for (int k = 0; k < kButtonCount; ++k)
            if (key == keys[k].id) { lines[i] = key + " = " + keys[k].key; written[k] = true; }
    }
    if (!has_player1) { lines.push_back("[player1]"); player1_end = lines.size(); }
    std::vector<std::string> missing;
    for (int k = 0; k < kButtonCount; ++k)
        if (!written[k]) missing.push_back(std::string(keys[k].id) + " = " + keys[k].key);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(player1_end), missing.begin(), missing.end());
    std::ofstream out(dir / "keybinds.ini", std::ios::trunc);
    for (const auto& line : lines) out << line << "\n";
}

std::filesystem::path newest_online_save(const std::filesystem::path& dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "netplay", ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".paired") continue;
        const auto time = entry.last_write_time(ec);
        if (!ec && (newest.empty() || time > newest_time)) {
            newest = entry.path();
            newest_time = time;
        }
    }
    return newest;
}

}  // namespace kamo
