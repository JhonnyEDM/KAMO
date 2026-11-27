// KAMO Netplay (test build): online co-op over input-based lockstep.
//
// Opens a window with a small menu: pick your ROM, then host a session or join one by address.
// Every PC runs all the session's consoles joined by the virtual link cable; only button input
// crosses the network. In game the window shows this player's console.
//
// Command line (testing):
//   KAMO-Netplay --host [--players 2-4] [--delay 0-8] [--port N] [--rom <file>]
//   KAMO-Netplay --join <address> [--port N] [--rom <file>]
//   ... --headless --frames N --input <script> [--dump-frame <bmp>] [--loopback]
// --delay 0 (default) picks the input delay from the measured ping.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/bmp.h"
#include "app/config.h"
#include "app/theme.h"
#include "gba/gba.h"
#include "gba/link.h"
#include "net/netplay.h"
#include "netplay/self_update.h"
#include "rom.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifndef KAMO_VERSION
#define KAMO_VERSION "dev"
#endif

namespace fs = std::filesystem;
using kamo::gba::Gba;
using kamo::gba::LinkCable;
namespace net = kamo::net;

namespace {

constexpr double kFrameSeconds = double(kamo::gba::kCyclesPerFrame) / double(kamo::gba::kCpuHz);
constexpr int kW = kamo::gba::kScreenWidth, kH = kamo::gba::kScreenHeight;

// ---- small helpers -------------------------------------------------------------------------

std::uint16_t read_keys(SDL_Gamepad* pad) {
    using namespace kamo::gba;
    const bool* k = SDL_GetKeyboardState(nullptr);
    std::uint16_t v = 0;
    auto set = [&](bool on, std::uint16_t bit) { if (on) v |= bit; };
    set(k[SDL_SCANCODE_X], kKeyA);
    set(k[SDL_SCANCODE_Z], kKeyB);
    set(k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT], kKeySelect);
    set(k[SDL_SCANCODE_RETURN], kKeyStart);
    set(k[SDL_SCANCODE_RIGHT], kKeyRight);
    set(k[SDL_SCANCODE_LEFT], kKeyLeft);
    set(k[SDL_SCANCODE_UP], kKeyUp);
    set(k[SDL_SCANCODE_DOWN], kKeyDown);
    set(k[SDL_SCANCODE_S], kKeyR);
    set(k[SDL_SCANCODE_A], kKeyL);
    if (pad) {
        auto b = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
        const int dead = 16000;
        int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        set(b(SDL_GAMEPAD_BUTTON_SOUTH) || b(SDL_GAMEPAD_BUTTON_NORTH), kKeyA);
        set(b(SDL_GAMEPAD_BUTTON_EAST) || b(SDL_GAMEPAD_BUTTON_WEST), kKeyB);
        set(b(SDL_GAMEPAD_BUTTON_BACK), kKeySelect);
        set(b(SDL_GAMEPAD_BUTTON_START), kKeyStart);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > dead, kKeyRight);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -dead, kKeyLeft);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -dead, kKeyUp);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > dead, kKeyDown);
        set(b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead, kKeyR);
        set(b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead, kKeyL);
    }
    if ((v & kKeyLeft) && (v & kKeyRight)) v &= std::uint16_t(~(kKeyLeft | kKeyRight));
    if ((v & kKeyUp) && (v & kKeyDown)) v &= std::uint16_t(~(kKeyUp | kKeyDown));
    return v;
}

// Scripted input "frame:hexkeys,..." (as in KAMO --input): keys held from each listed frame.
std::uint16_t script_keys(const std::string& script, std::uint32_t frame) {
    std::uint16_t keys = 0;
    std::size_t p = 0;
    while (p < script.size()) {
        std::size_t comma = script.find(',', p);
        std::string item = script.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
        std::size_t colon = item.find(':');
        if (colon != std::string::npos) {
            if (std::strtoul(item.c_str(), nullptr, 10) > frame) break;
            keys = std::uint16_t(std::strtoul(item.c_str() + colon + 1, nullptr, 16));
        }
        if (comma == std::string::npos) break;
        p = comma + 1;
    }
    return keys;
}

// This PC's IPv4 addresses, with a hint for virtual LANs.
std::vector<std::pair<std::string, std::string>> local_addresses() {
    std::vector<std::pair<std::string, std::string>> out;
    auto hint = [](const std::string& ip) -> std::string {
        if (ip.rfind("26.", 0) == 0) return "Radmin VPN";
        if (ip.rfind("100.", 0) == 0) return "Tailscale";
        if (ip.rfind("192.168.", 0) == 0 || ip.rfind("10.", 0) == 0 || ip.rfind("172.", 0) == 0) return "local network";
        return "";
    };
    auto add = [&](const std::string& ip) {
        if (ip.rfind("127.", 0) == 0 || ip.rfind("169.254.", 0) == 0) return;
        for (auto& e : out)
            if (e.first == ip) return;
        out.emplace_back(ip, hint(ip));
    };
#ifdef _WIN32
    char name[256] = {};
    if (gethostname(name, sizeof name) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
            for (addrinfo* a = res; a; a = a->ai_next) {
                char buf[64] = {};
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr, buf, sizeof buf);
                add(buf);
            }
            freeaddrinfo(res);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            char buf[64] = {};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
            add(buf);
        }
        freeifaddrs(list);
    }
#endif
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return !a.second.empty() && b.second.empty(); });
    return out;
}

// key=value settings in the user folder.
struct Settings {
    std::string rom, address;
    int players = 2, delay = 0;
    fs::path file;
    void load() {
        char* base = SDL_GetPrefPath("KAMO", "Netplay");
        file = base ? fs::path(base) / "netplay.ini" : fs::path("netplay.ini");
        SDL_free(base);
        std::ifstream f(file);
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "rom") rom = v;
            else if (k == "address") address = v;
            else if (k == "players") players = std::clamp(std::atoi(v.c_str()), 2, 4);
            else if (k == "delay") delay = std::clamp(std::atoi(v.c_str()), 0, net::kMaxDelay);
        }
    }
    void save() const {
        std::ofstream f(file);
        f << "rom=" << rom << "\naddress=" << address << "\nplayers=" << players << "\ndelay=" << delay << "\n";
    }
};

std::uint64_t state_hash(const std::vector<std::unique_ptr<Gba>>& consoles) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::span<const std::uint8_t> m) {
        for (std::uint8_t b : m) {
            h ^= b;
            h *= 1099511628211ull;
        }
    };
    for (const auto& c : consoles) {
        mix(c->ewram());
        mix(c->iwram());
    }
    return h;
}

// ---- the running game ----------------------------------------------------------------------

struct Match {
    std::vector<std::unique_ptr<Gba>> consoles;
    std::unique_ptr<LinkCable> cable;
    std::uint32_t frame = 0, next_submit = 0;
    std::uint64_t stalls = 0;           // frames that had to wait for someone's input
    long long stalled_frame = -1;
    double acc = 0, ahead = 0;          // pacing: time owed, smoothed frames ahead of the others
    std::vector<std::int16_t> audio, discard;
    // Recording of every player's input, one line per frame, so a session can be replayed with
    // `kamo_link --replay <file>` to reproduce problems exactly.
    std::ofstream rec;
    fs::path rec_path;

    bool active() const { return !consoles.empty(); }

    fs::path session_dir, local_save, keep_save;
    fs::file_time_type last_sync{};

    // Copies this player's save from the session to their own save file when it changed.
    void sync_save() {
        if (keep_save.empty()) return;
        std::error_code ec;
        if (!fs::exists(local_save, ec)) return;
        auto t = fs::last_write_time(local_save, ec);
        if (ec || t == last_sync) return;
        fs::create_directories(keep_save.parent_path(), ec);
        fs::path tmp = keep_save;
        tmp += ".tmp";
        fs::copy_file(local_save, tmp, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::rename(tmp, keep_save, ec);
        if (!ec) last_sync = t;
    }

    void start_recording(int players, int delay, int slot) {
        char* base = SDL_GetPrefPath("KAMO", "Netplay");
        fs::path dir = (base ? fs::path(base) : fs::temp_directory_path()) / "recordings";
        SDL_free(base);
        std::error_code ec;
        fs::create_directories(dir, ec);
        // Keep the 10 newest recordings.
        std::vector<fs::path> old;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".kinp") old.push_back(e.path());
        std::sort(old.begin(), old.end());
        while (old.size() >= 10) {
            fs::remove(old.front(), ec);
            old.erase(old.begin());
        }
        SDL_DateTime t{};
        SDL_Time now = 0;
        SDL_GetCurrentTime(&now);
        SDL_TimeToDateTime(now, &t, true);
        char name[64];
        std::snprintf(name, sizeof name, "session-%04d%02d%02d-%02d%02d%02d.kinp", t.year, t.month, t.day, t.hour,
                      t.minute, t.second);
        rec_path = dir / name;
        rec.open(rec_path);
        rec << "KAMO-INPUT 1\nversion " << KAMO_VERSION << "\nplayers " << players << "\ndelay " << delay
            << "\nlocal " << slot + 1 << "\nframes\n";
    }

    // Boots every console with the save its player brought. The local player's progress is copied
    // back to `user_save` while playing and at the end (empty path: not kept).
    void boot(const std::vector<std::uint8_t>& rom, const net::Session& s, fs::path user_save = {}) {
        const int players = s.players();
        session_dir = fs::temp_directory_path() / "kamo-netplay";
        fs::path& dir = session_dir;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        local_save = dir / ("p" + std::to_string(s.local_slot() + 1) + ".sav");
        keep_save = std::move(user_save);
        last_sync = {};
        std::vector<Gba*> ptrs;
        for (int i = 0; i < players; ++i) {
            const auto& data = s.save_of(i);
            if (!data.empty()) {
                std::ofstream f(dir / ("p" + std::to_string(i + 1) + ".sav"), std::ios::binary);
                f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            }
            consoles.push_back(std::make_unique<Gba>(rom, dir / ("p" + std::to_string(i + 1) + ".sav")));
            consoles.back()->reset();
            ptrs.push_back(consoles.back().get());
        }
        cable = std::make_unique<LinkCable>(ptrs);
        frame = next_submit = 0;
        stalls = 0;
        stalled_frame = -1;
        acc = ahead = 0;
    }

    void end() {
        cable.reset();
        consoles.clear();
        sync_save();  // the consoles flushed their saves when destroyed
        if (rec.is_open()) rec.close();
    }

    // Advances as many frames as are due. Returns false once `quit_frame` is reached.
    template <class Keys>
    bool advance(net::Session& s, double dt, Keys keys_for, SDL_AudioStream* stream, long quit_frame) {
        // Pacing: a PC ahead of the others runs up to 3 % slower (behind: faster), so lockstep
        // rarely has to stop and wait.
        ahead = ahead * 0.9 + s.frames_ahead(frame, kFrameSeconds) * 0.1;
        const double period = kFrameSeconds * (1.0 + std::clamp(ahead * 0.02, -0.03, 0.03));
        acc = std::min(acc + dt, 0.25);
        while (acc >= period) {
            while (next_submit <= frame) {
                s.submit_input(next_submit + std::uint32_t(s.delay()), keys_for(next_submit), frame);
                ++next_submit;
            }
            if (!s.ready(frame)) {
                if (stalled_frame != frame) {
                    ++stalls;
                    stalled_frame = frame;
                }
                acc = std::min(acc, period);  // wait, but don't pile up time
                break;
            }
            for (int i = 0; i < s.players(); ++i) consoles[size_t(i)]->set_keys(s.input(i, frame));
            if (rec.is_open()) {
                char line[32];
                int n = 0;
                for (int i = 0; i < s.players(); ++i)
                    n += std::snprintf(line + n, sizeof line - size_t(n), i ? " %03x" : "%03x", s.input(i, frame));
                rec << line << '\n';
                if (frame % 600 == 0) rec.flush();
            }
            cable->run_frame();
            ++frame;
            acc -= period;
            for (int i = 0; i < s.players(); ++i)
                consoles[size_t(i)]->take_audio(i == s.local_slot() ? audio : discard);
            discard.clear();
            if (stream && !audio.empty() && SDL_GetAudioStreamQueued(stream) < kamo::gba::kAudioRate / 5 * 4)
                SDL_PutAudioStreamData(stream, audio.data(), int(audio.size() * sizeof(std::int16_t)));
            audio.clear();
            if (frame % 60 == 0) s.submit_hash(frame, state_hash(consoles));
            if (frame % 300 == 0) sync_save();
            if (quit_frame > 0 && long(frame) >= quit_frame) return false;
        }
        return true;
    }
};

// ---- command line --------------------------------------------------------------------------

struct Options {
    bool host = false, join = false, headless = false, loopback = false;
    std::string address, script;
    std::uint16_t port = net::kDefaultPort;
    int players = 2, delay = 0;
    long quit_frame = 0;
    fs::path rom, dump;
    std::string update_test, pretend_version;
    fs::path save;
};

int usage() {
    std::fprintf(stderr,
                 "usage: KAMO-Netplay [--host [--players 2-4] [--delay 0-8] | --join <address>] [--port N]\n"
                 "                    [--rom <file>] [--headless --frames N --input <script> --dump-frame <bmp>]\n");
    return 2;
}

// Headless run for automated tests: no window, scripted input, prints the final state hash.
int run_headless(const Options& opt, const kamo::Rom& rom) {
    net::Config cfg;
    cfg.host = opt.host;
    cfg.address = opt.address;
    cfg.port = opt.port;
    cfg.players = opt.players;
    cfg.delay = opt.delay;
    cfg.version = KAMO_VERSION;
    cfg.rom_sha1 = rom.sha1;
    cfg.loopback_only = opt.loopback;
    if (!opt.save.empty()) {
        std::ifstream in(opt.save, std::ios::binary);
        cfg.save.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    net::Session s;
    std::string err;
    if (!s.start(cfg, &err)) {
        std::printf("network error: %s\n", err.c_str());
        return 1;
    }
    Match m;
    std::string last;
    std::uint64_t tick = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    for (;;) {
        s.poll();
        if (s.message() != last && s.state() != net::State::Running) std::printf("%s\n", (last = s.message()).c_str());
        if (s.state() == net::State::Failed) return 1;
        if (s.state() == net::State::Running && !m.active()) {
            m.boot(rom.data, s);
            std::printf("session started: player %d of %d, input delay %d\n", s.local_slot() + 1, s.players(), s.delay());
            tick = SDL_GetPerformanceCounter();
        }
        std::uint64_t now = SDL_GetPerformanceCounter();
        double dt = double(now - tick) / freq;
        tick = now;
        if (m.active() &&
            !m.advance(s, dt, [&](std::uint32_t f) { return script_keys(opt.script, f); }, nullptr, opt.quit_frame))
            break;
        SDL_Delay(1);
    }
    std::printf("final frame %u hash %016llx stalls %llu desync %lld\n", m.frame,
                (unsigned long long)state_hash(m.consoles), (unsigned long long)m.stalls, s.desync_frame());
    if (!opt.dump.empty()) kamo::write_bmp(opt.dump.string(), m.consoles[size_t(s.local_slot())]->framebuffer());
    // Let the last input reach the other players before leaving.
    for (int i = 0; i < 50; ++i) {
        s.poll();
        SDL_Delay(10);
    }
    return 0;
}

// ---- file dialog ---------------------------------------------------------------------------

std::mutex g_pick_mutex;
std::optional<std::string> g_picked;

void SDLCALL on_rom_picked(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(g_pick_mutex);
    g_picked = files[0];
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--host") opt.host = true;
        else if (a == "--join") { opt.join = true; opt.address = next(); }
        else if (a == "--port") opt.port = std::uint16_t(std::atoi(next().c_str()));
        else if (a == "--players") opt.players = std::clamp(std::atoi(next().c_str()), 2, 4);
        else if (a == "--delay") opt.delay = std::clamp(std::atoi(next().c_str()), 0, net::kMaxDelay);
        else if (a == "--rom") opt.rom = next();
        else if (a == "--headless") opt.headless = true;
        else if (a == "--loopback") opt.loopback = true;
        else if (a == "--frames") opt.quit_frame = std::strtol(next().c_str(), nullptr, 10);
        else if (a == "--input") opt.script = next();
        else if (a == "--dump-frame") opt.dump = next();
        else if (a == "--save") opt.save = next();  // headless: battery save to bring
        else if (a == "--update-test") opt.update_test = next();  // "check" or "install" (testing)
        else if (a == "--pretend-version") opt.pretend_version = next();
        else return usage();
    }

    if (!opt.update_test.empty()) {
        // Self-update test: check (and optionally install) as if this were pretend_version.
        SDL_Init(0);
        kamo::netplay::SelfUpdate u(opt.pretend_version.empty() ? KAMO_VERSION : opt.pretend_version);
        using US = kamo::netplay::SelfUpdate::State;
        u.check();
        while (u.state() == US::Checking) SDL_Delay(20);
        std::printf("check: state %d version %s error %s\n", int(u.state()), u.version().c_str(), u.error().c_str());
        if (opt.update_test == "install" && u.state() == US::Available) {
            u.install();
            while (u.state() == US::Downloading) SDL_Delay(20);
            std::printf("install: state %d error %s\n", int(u.state()), u.error().c_str());
        }
        SDL_Quit();
        return 0;
    }

    if (opt.headless) {
        if (opt.rom.empty() || (!opt.host && !opt.join)) return usage();
        SDL_Init(0);
        auto loaded = kamo::load_rom(opt.rom);
        if (!loaded.ok || !loaded.rom.hash_matches) {
            std::printf("ROM not accepted\n");
            return 1;
        }
        int rc = run_headless(opt, loaded.rom);
        SDL_Quit();
        return rc;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);  // for local_addresses(); ENet starts its own
#endif
    kamo::netplay::SelfUpdate::cleanup();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return 1;
    Settings settings;
    settings.load();
    if (!opt.rom.empty()) settings.rom = opt.rom.string();

    SDL_Window* window = SDL_CreateWindow("KAMO Netplay", kW * 4, kH * 4 + 80, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowMinimumSize(window, kW * 2, kH * 2);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR1555, SDL_TEXTUREACCESS_STREAMING, kW, kH);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kamo::gba::kAudioRate};
    SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    SDL_Gamepad* pad = nullptr;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    kamo::theme::apply(std::max(1.0f, SDL_GetWindowDisplayScale(window)));
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    const kamo::theme::Palette& c = kamo::theme::palette();

    // ROM state.
    std::optional<kamo::Rom> rom;
    std::string rom_status;
    auto try_rom = [&](const std::string& path) {
        rom.reset();
        if (path.empty()) {
            rom_status.clear();
            return;
        }
        auto loaded = kamo::load_rom(path);
        if (loaded.ok && loaded.rom.hash_matches) {
            rom = std::move(loaded.rom);
            rom_status = "Verified";
            settings.rom = path;
            settings.save();
        } else {
            rom_status = loaded.ok ? (loaded.rom.other_release.empty() ? "Not Kirby & The Amazing Mirror (USA)"
                                                                       : "This is " + loaded.rom.other_release + "; the USA release is needed")
                                   : loaded.error;
        }
    };
    try_rom(settings.rom);

    char address[128] = {};
    std::snprintf(address, sizeof address, "%s", (opt.join ? opt.address : settings.address).c_str());
    int players_choice = opt.host ? opt.players : settings.players;
    int delay_choice = opt.host ? opt.delay : settings.delay;
    auto addresses = local_addresses();

    kamo::netplay::SelfUpdate updater(KAMO_VERSION);
    updater.check();

    enum class Screen { Menu, Session } screen = Screen::Menu;
    net::Session session;
    Match match;
    std::string notice;            // shown on the menu (why the last session ended)
    bool overlay = true;
    double started_at = 0;

    // The same battery save KAMO.exe uses, so progress carries over between single player and online.
    kamo::Config kamo_config;
    auto user_save_path = [&]() -> fs::path {
        return rom ? kamo_config.pref_dir() / "saves" / (rom->sha1 + ".sav") : fs::path();
    };
    // Copy of the save before each online session (10 newest kept), in case anything goes wrong.
    auto backup_save = [&](const fs::path& save) {
        std::error_code ec;
        if (!fs::exists(save, ec)) return;
        fs::path dir = save.parent_path() / "backups";
        fs::create_directories(dir, ec);
        std::vector<fs::path> old;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().filename().string().rfind("netplay-", 0) == 0) old.push_back(e.path());
        std::sort(old.begin(), old.end());
        while (old.size() >= 10) {
            fs::remove(old.front(), ec);
            old.erase(old.begin());
        }
        SDL_Time now = 0;
        SDL_DateTime t{};
        SDL_GetCurrentTime(&now);
        SDL_TimeToDateTime(now, &t, true);
        char name[64];
        std::snprintf(name, sizeof name, "netplay-%04d%02d%02d-%02d%02d%02d.sav", t.year, t.month, t.day, t.hour, t.minute, t.second);
        fs::copy_file(save, dir / name, fs::copy_options::overwrite_existing, ec);
    };

    auto start_session = [&](bool host) {
        if (!rom) return;
        net::Config cfg;
        if (fs::path save = user_save_path(); !save.empty()) {
            backup_save(save);
            std::ifstream in(save, std::ios::binary);
            cfg.save.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        cfg.host = host;
        cfg.address = address;
        cfg.port = opt.port;
        cfg.players = players_choice;
        cfg.delay = delay_choice;
        cfg.version = KAMO_VERSION;
        cfg.rom_sha1 = rom->sha1;
        cfg.name = host ? "host" : "player";
        std::string err;
        if (!session.start(cfg, &err)) {
            notice = err;
            return;
        }
        settings.address = address;
        settings.players = players_choice;
        settings.delay = delay_choice;
        settings.save();
        notice.clear();
        screen = Screen::Session;
    };
    auto leave_session = [&](const std::string& why) {
        session.stop();
        match.end();
        if (audio) SDL_ClearAudioStream(audio);
        notice = why;
        screen = Screen::Menu;
    };
    if (rom && (opt.host || opt.join)) start_session(opt.host);

    std::uint64_t tick = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = nullptr;
            } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.scancode == SDL_SCANCODE_ESCAPE && screen == Screen::Session) leave_session("You left the session.");
                else if (e.key.scancode == SDL_SCANCODE_F2) overlay = !overlay;
                else if (e.key.scancode == SDL_SCANCODE_F11)
                    SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_pick_mutex);
            if (g_picked) {
                try_rom(*g_picked);
                g_picked.reset();
            }
        }

        std::uint64_t now = SDL_GetPerformanceCounter();
        double dt = double(now - tick) / freq;
        tick = now;

        if (screen == Screen::Session) {
            session.poll();
            if (session.state() == net::State::Failed) leave_session(session.message());
            else if (session.state() == net::State::Running) {
                if (!match.active()) {
                    match.boot(rom->data, session, user_save_path());
                    match.start_recording(session.players(), session.delay(), session.local_slot());
                    started_at = ImGui::GetTime();
                    dt = 0;
                }
                match.advance(session, dt, [&](std::uint32_t) { return read_keys(pad); }, audio, 0);
            }
        }

        // ---- draw ----
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImVec2 ws = io.DisplaySize;
        const bool in_game = screen == Screen::Session && match.active();

        if (!in_game) kamo::theme::draw_backdrop(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), ws);

        if (screen == Screen::Menu) {
            const float w = std::min(ws.x - 40.0f, 560.0f * io.FontGlobalScale);
            ImGui::SetNextWindowPos(ImVec2(ws.x * 0.5f, ws.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(w, 0));
            ImGui::Begin("menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushFont(kamo::theme::font_title());
            ImGui::TextUnformatted("KAMO Netplay");
            ImGui::PopFont();
            ImGui::PushFont(kamo::theme::font_small());
            ImGui::TextColored(c.ink_faint, "Online co-op test build  ·  version %s", KAMO_VERSION);
            ImGui::PopFont();

            // Update banner.
            auto us = updater.state();
            if (us == kamo::netplay::SelfUpdate::State::Available) {
                ImGui::Spacing();
                ImGui::TextColored(c.highlight, "Version %s is available.", updater.version().c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Update now")) updater.install();
            } else if (us == kamo::netplay::SelfUpdate::State::Downloading) {
                ImGui::Spacing();
                ImGui::ProgressBar(updater.progress(), ImVec2(-1, 0), "Downloading update...");
            } else if (us == kamo::netplay::SelfUpdate::State::Ready) {
                ImGui::Spacing();
                ImGui::TextColored(c.good, "Update installed.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Restart")) {
                    updater.relaunch();
                    running = false;
                }
            } else if (us == kamo::netplay::SelfUpdate::State::Failed && !updater.error().empty()) {
                ImGui::PushFont(kamo::theme::font_small());
                ImGui::TextColored(c.ink_faint, "Update check failed: %s", updater.error().c_str());
                ImGui::PopFont();
            }

            ImGui::Separator();
            // ROM.
            ImGui::PushFont(kamo::theme::font_button());
            ImGui::TextUnformatted("Your ROM");
            ImGui::PopFont();
            if (!settings.rom.empty()) {
                ImGui::TextWrapped("%s", fs::path(settings.rom).filename().string().c_str());
                if (rom) ImGui::TextColored(c.good, KAMO_ICON_CHECK " %s", rom_status.c_str());
                if (rom) {
                    ImGui::PushFont(kamo::theme::font_small());
                    std::error_code ec;
                    ImGui::TextColored(c.ink_muted, fs::exists(user_save_path(), ec)
                                                        ? "Your progress is saved to the same file as KAMO.exe."
                                                        : "No save yet: your progress will be saved for KAMO and KAMO.exe.");
                    ImGui::PopFont();
                }
                else if (!rom_status.empty()) ImGui::TextColored(c.bad, "%s", rom_status.c_str());
            } else {
                ImGui::TextColored(c.ink_muted, "Kirby & The Amazing Mirror (USA). It is never copied or sent.");
            }
            if (ImGui::Button(KAMO_ICON_FOLDER_OPEN " Choose ROM...")) {
                static const SDL_DialogFileFilter filters[] = {{"Game Boy Advance ROM", "gba;agb;bin;zip"}, {"All files", "*"}};
                SDL_ShowOpenFileDialog(on_rom_picked, nullptr, window, filters, 2, nullptr, false);
            }

            ImGui::BeginDisabled(!rom);
            ImGui::Separator();
            // Host.
            ImGui::PushFont(kamo::theme::font_button());
            ImGui::TextUnformatted("Host a session");
            ImGui::PopFont();
            ImGui::SetNextItemWidth(140);
            const char* counts[] = {"2 players", "3 players", "4 players"};
            int pi = players_choice - 2;
            if (ImGui::Combo("##players", &pi, counts, 3)) players_choice = pi + 2;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            const char* delays[] = {"Delay: auto", "Delay: 1", "Delay: 2", "Delay: 3", "Delay: 4",
                                    "Delay: 5", "Delay: 6", "Delay: 7", "Delay: 8"};
            ImGui::Combo("##delay", &delay_choice, delays, 9);
            ImGui::SameLine();
            if (ImGui::Button(KAMO_ICON_PLAY " Host")) start_session(true);
            if (!addresses.empty()) {
                ImGui::PushFont(kamo::theme::font_small());
                ImGui::TextColored(c.ink_muted, "Give your friends one of your addresses (click to copy):");
                for (auto& [ip, label] : addresses) {
                    std::string text = label.empty() ? ip : ip + "  (" + label + ")";
                    if (ImGui::Selectable(text.c_str(), false)) ImGui::SetClipboardText(ip.c_str());
                }
                ImGui::PopFont();
            }

            ImGui::Separator();
            // Join.
            ImGui::PushFont(kamo::theme::font_button());
            ImGui::TextUnformatted("Join a session");
            ImGui::PopFont();
            ImGui::SetNextItemWidth(std::max(160.0f, w - 160.0f));
            bool enter = ImGui::InputTextWithHint("##address", "Host address, e.g. 26.130.64.180", address, sizeof address,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            ImGui::BeginDisabled(address[0] == 0);
            if (ImGui::Button(KAMO_ICON_CHEVRON_RIGHT " Join") || (enter && address[0])) start_session(false);
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            if (!notice.empty()) {
                ImGui::Separator();
                ImGui::TextColored(c.bad, "%s", notice.c_str());
            }
            ImGui::PushFont(kamo::theme::font_small());
            ImGui::Separator();
            ImGui::TextColored(c.ink_faint,
                               "Keys: arrows, X = A, Z = B, A/S = L/R, Enter = Start, Backspace = Select.\n"
                               "F2 = connection info, F11 = fullscreen, Esc = leave the session.");
            ImGui::PopFont();
            ImGui::End();
        } else if (!in_game) {
            // Waiting room.
            ImGui::SetNextWindowPos(ImVec2(ws.x * 0.5f, ws.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(ws.x - 40.0f, 480.0f), 0));
            ImGui::Begin("lobby", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushFont(kamo::theme::font_button());
            ImGui::TextUnformatted(session.local_slot() == 0 && session.state() != net::State::Connecting ? "Hosting"
                                                                                                         : "Joining");
            ImGui::PopFont();
            ImGui::TextWrapped("%s", session.message().c_str());
            if (session.local_slot() == 0 && !addresses.empty() && session.state() == net::State::Waiting &&
                opt.port == net::kDefaultPort) {
                ImGui::PushFont(kamo::theme::font_small());
                ImGui::TextColored(c.ink_muted, "Your address: %s%s", addresses[0].first.c_str(),
                                   addresses[0].second.empty() ? "" : ("  (" + addresses[0].second + ")").c_str());
                ImGui::PopFont();
            }
            ImGui::Spacing();
            if (ImGui::Button(KAMO_ICON_X " Cancel")) leave_session("");
            ImGui::End();
        } else {
            // Game overlay.
            const bool desync = session.desync_frame() >= 0;
            if (overlay || desync) {
                ImGui::SetNextWindowPos(ImVec2(8, 8));
                ImGui::SetNextWindowBgAlpha(0.55f);
                ImGui::Begin("hud", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                                 ImGuiWindowFlags_NoFocusOnAppearing);
                ImGui::PushFont(kamo::theme::font_small());
                ImGui::Text("Player %d/%d  ·  ping %d ms  ·  delay %d  ·  waits %llu", session.local_slot() + 1,
                            session.players(), session.ping_ms(), session.delay(), (unsigned long long)match.stalls);
                if (desync) ImGui::TextColored(c.bad, "DESYNC at frame %lld", session.desync_frame());
                if (ImGui::GetTime() - started_at < 12.0)
                    ImGui::TextColored(c.ink_muted, "Start > START GAME > MULTI PLAYER on every console. F2 hides this.");
                ImGui::PopFont();
                ImGui::End();
            }
        }
        ImGui::Render();

        // Window title with the session state (also read by tools).
        std::string title = "KAMO Netplay";
        if (in_game) {
            title += " - Player " + std::to_string(session.local_slot() + 1) + "/" + std::to_string(session.players()) +
                     " - ping " + std::to_string(session.ping_ms()) + " ms - delay " + std::to_string(session.delay()) +
                     " - frame " + std::to_string(match.frame) + " - waits " + std::to_string(match.stalls);
            if (session.desync_frame() >= 0) title += " - DESYNC at frame " + std::to_string(session.desync_frame());
        } else if (screen == Screen::Session) {
            title += " - " + session.message();
        }
        static std::string last_title;
        if (title != last_title) SDL_SetWindowTitle(window, (last_title = title).c_str());

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (in_game) {
            int ow = 0, oh = 0;
            SDL_GetRenderOutputSize(renderer, &ow, &oh);
            int scale = std::max(1, std::min(ow / kW, oh / kH));
            SDL_FRect dst{float((ow - kW * scale) / 2), float((oh - kH * scale) / 2), float(kW * scale), float(kH * scale)};
            SDL_UpdateTexture(tex, nullptr, match.consoles[size_t(session.local_slot())]->framebuffer(), kW * 2);
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        if (!in_game) SDL_Delay(5);
    }

    session.stop();
    match.end();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (pad) SDL_CloseGamepad(pad);
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
