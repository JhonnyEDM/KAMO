// KAMO Netplay (test build): online co-op over input-based lockstep.
//
//   KAMO-Netplay --host [--players 2] [--port 7845] [--delay 3] [--rom <file>]
//   KAMO-Netplay --join <ip or host name> [--port 7845] [--rom <file>]
//
// Started without arguments it asks for everything in the console window. Every PC runs all the
// session's consoles joined by the virtual link cable; only button input crosses the network.
// The window shows this player's console. Sessions start from a fresh game (no save files yet).

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "gba/gba.h"
#include "gba/link.h"
#include "net/netplay.h"
#include "app/bmp.h"
#include "rom.h"

#ifndef KAMO_VERSION
#define KAMO_VERSION "dev"
#endif

namespace fs = std::filesystem;
using kamo::gba::Gba;
using kamo::gba::LinkCable;

namespace {

constexpr double kFrameSeconds = double(kamo::gba::kCyclesPerFrame) / double(kamo::gba::kCpuHz);

struct Options {
    bool host = false, join = false;
    std::string address;
    std::uint16_t port = kamo::net::kDefaultPort;
    int players = 2, delay = 3, scale = 3;
    fs::path rom;
    // Automated testing: no window or sound, scripted local input, quit after a frame.
    bool headless = false;
    bool loopback = false;
    long quit_frame = 0;
    std::string script;
    fs::path dump;
};

std::string ask(const std::string& prompt, const std::string& def = "") {
    std::cout << prompt;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
    if (line.size() >= 2 && line.front() == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
    return line.empty() ? def : line;
}

fs::path remembered_rom_file() {
    char* base = SDL_GetPrefPath("KAMO", "Netplay");
    fs::path p = base ? fs::path(base) / "rom.txt" : fs::path("kamo-netplay-rom.txt");
    SDL_free(base);
    return p;
}

std::uint16_t read_keys(SDL_Gamepad* pad) {
    const bool* k = SDL_GetKeyboardState(nullptr);
    std::uint16_t v = 0;
    auto set = [&](bool on, std::uint16_t bit) { if (on) v |= bit; };
    set(k[SDL_SCANCODE_X], kamo::gba::kKeyA);
    set(k[SDL_SCANCODE_Z], kamo::gba::kKeyB);
    set(k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT], kamo::gba::kKeySelect);
    set(k[SDL_SCANCODE_RETURN], kamo::gba::kKeyStart);
    set(k[SDL_SCANCODE_RIGHT], kamo::gba::kKeyRight);
    set(k[SDL_SCANCODE_LEFT], kamo::gba::kKeyLeft);
    set(k[SDL_SCANCODE_UP], kamo::gba::kKeyUp);
    set(k[SDL_SCANCODE_DOWN], kamo::gba::kKeyDown);
    set(k[SDL_SCANCODE_S], kamo::gba::kKeyR);
    set(k[SDL_SCANCODE_A], kamo::gba::kKeyL);
    if (pad) {
        auto b = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
        const int dead = 16000;
        int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        set(b(SDL_GAMEPAD_BUTTON_SOUTH) || b(SDL_GAMEPAD_BUTTON_NORTH), kamo::gba::kKeyA);
        set(b(SDL_GAMEPAD_BUTTON_EAST) || b(SDL_GAMEPAD_BUTTON_WEST), kamo::gba::kKeyB);
        set(b(SDL_GAMEPAD_BUTTON_BACK), kamo::gba::kKeySelect);
        set(b(SDL_GAMEPAD_BUTTON_START), kamo::gba::kKeyStart);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > dead, kamo::gba::kKeyRight);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -dead, kamo::gba::kKeyLeft);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -dead, kamo::gba::kKeyUp);
        set(b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > dead, kamo::gba::kKeyDown);
        set(b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead,
            kamo::gba::kKeyR);
        set(b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead,
            kamo::gba::kKeyL);
    }
    // Opposite directions at once confuse some games.
    if ((v & kamo::gba::kKeyLeft) && (v & kamo::gba::kKeyRight)) v &= std::uint16_t(~(kamo::gba::kKeyLeft | kamo::gba::kKeyRight));
    if ((v & kamo::gba::kKeyUp) && (v & kamo::gba::kKeyDown)) v &= std::uint16_t(~(kamo::gba::kKeyUp | kamo::gba::kKeyDown));
    return v;
}

// Scripted input "frame:hexkeys,frame:hexkeys,..." (as in KAMO --input): keys held from each
// listed frame until the next entry.
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

int usage() {
    std::fprintf(stderr,
                 "usage: KAMO-Netplay --host [--players 2-4] [--port N] [--delay 1-10] [--rom <file>]\n"
                 "       KAMO-Netplay --join <ip> [--port N] [--rom <file>]\n");
    return 2;
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
        else if (a == "--players") opt.players = std::atoi(next().c_str());
        else if (a == "--delay") opt.delay = std::atoi(next().c_str());
        else if (a == "--scale") opt.scale = std::clamp(std::atoi(next().c_str()), 1, 8);
        else if (a == "--rom") opt.rom = next();
        else if (a == "--headless") opt.headless = true;
        else if (a == "--loopback") opt.loopback = true;
        else if (a == "--frames") opt.quit_frame = std::strtol(next().c_str(), nullptr, 10);
        else if (a == "--input") opt.script = next();
        else if (a == "--dump-frame") opt.dump = next();
        else if (a == "-h" || a == "--help") return usage();
        else return usage();
    }

    std::cout << "KAMO Netplay " << KAMO_VERSION << " (test build)\n\n";

    // ROM: argument, else the one used last time, else ask.
    fs::path remember = remembered_rom_file();
    if (opt.rom.empty()) {
        std::ifstream f(remember);
        std::string last;
        if (f) std::getline(f, last);
        std::string answer = ask("Path to your Kirby & The Amazing Mirror (USA) ROM", last);
        opt.rom = answer;
    }
    auto loaded = kamo::load_rom(opt.rom);
    if (!loaded.ok || !loaded.rom.hash_matches) {
        std::cout << "\nThat file is not the supported ROM: "
                  << (loaded.ok ? "it must be Kirby & The Amazing Mirror (USA)." : loaded.error) << "\n";
        ask("Press Enter to close");
        return 1;
    }
    { std::ofstream(remember) << opt.rom.string() << "\n"; }
    std::cout << "ROM verified.\n\n";

    if (!opt.host && !opt.join) {
        std::string mode = ask("Host a session (h) or join one (j)?", "h");
        if (!mode.empty() && (mode[0] == 'j' || mode[0] == 'J')) {
            opt.join = true;
            opt.address = ask("Host IP address");
        } else {
            opt.host = true;
            opt.players = std::atoi(ask("Number of players (2-4)", "2").c_str());
        }
    }
    if (opt.join && opt.address.empty()) return usage();

    kamo::net::Config cfg;
    cfg.host = opt.host;
    cfg.address = opt.address;
    cfg.port = opt.port;
    cfg.players = std::clamp(opt.players, 2, kamo::net::kMaxPlayers);
    cfg.delay = opt.delay;
    cfg.version = KAMO_VERSION;
    cfg.rom_sha1 = loaded.rom.sha1;
    cfg.name = opt.host ? "host" : "player";
    cfg.loopback_only = opt.loopback;

    kamo::net::Session net;
    std::string error;
    if (!net.start(cfg, &error)) {
        std::cout << "Network error: " << error << "\n";
        ask("Press Enter to close");
        return 1;
    }
    if (opt.host)
        std::cout << "Hosting on port " << opt.port << " (UDP). Give your friends this PC's IP address.\n"
                  << "Allow KAMO-Netplay through the Windows firewall if asked.\n\n";

    if (!SDL_Init(opt.headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
        std::cout << "SDL error: " << SDL_GetError() << "\n";
        return 1;
    }
    const int ww = kamo::gba::kScreenWidth * opt.scale, wh = kamo::gba::kScreenHeight * opt.scale;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* tex = nullptr;
    SDL_AudioStream* audio = nullptr;
    if (!opt.headless) {
    window = SDL_CreateWindow("KAMO Netplay", ww, wh, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderLogicalPresentation(renderer, kamo::gba::kScreenWidth, kamo::gba::kScreenHeight,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR1555, SDL_TEXTUREACCESS_STREAMING,
                                         kamo::gba::kScreenWidth, kamo::gba::kScreenHeight);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kamo::gba::kAudioRate};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    }
    SDL_Gamepad* pad = nullptr;

    std::vector<std::unique_ptr<Gba>> consoles;
    std::unique_ptr<LinkCable> cable;
    fs::path session_dir = fs::temp_directory_path() / "kamo-netplay";
    std::vector<std::int16_t> audio_buf, discard;

    std::uint32_t frame = 0, next_submit = 0;
    std::uint64_t stalls = 0;
    std::uint64_t last_tick = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    double acc = 0;
    std::string last_title, last_message;
    bool desync_reported = false;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = nullptr;
            }
        }
        net.poll();
        const kamo::net::State st = net.state();

        // Session started: boot every console the same way on every PC.
        if (st == kamo::net::State::Running && consoles.empty()) {
            fs::remove_all(session_dir);
            fs::create_directories(session_dir);
            std::vector<Gba*> ptrs;
            for (int i = 0; i < net.players(); ++i) {
                consoles.push_back(std::make_unique<Gba>(loaded.rom.data, session_dir / ("p" + std::to_string(i + 1) + ".sav")));
                consoles.back()->reset();
                ptrs.push_back(consoles.back().get());
            }
            cable = std::make_unique<LinkCable>(ptrs);
            std::cout << "Session started: you are player " << net.local_slot() + 1 << " of " << net.players()
                      << ", input delay " << net.delay() << " frames.\n";
            last_tick = SDL_GetPerformanceCounter();
            acc = 0;
        }

        std::uint64_t now = SDL_GetPerformanceCounter();
        acc += double(now - last_tick) / freq;
        last_tick = now;
        if (acc > 0.25) acc = 0.25;  // don't try to catch up after a long stall

        if (st == kamo::net::State::Running && !consoles.empty()) {
            while (acc >= kFrameSeconds) {
                // Local input for frame + delay, sent once per frame.
                while (next_submit <= frame) {
                    net.submit_input(next_submit + std::uint32_t(net.delay()), opt.headless ? script_keys(opt.script, next_submit) : read_keys(pad));
                    ++next_submit;
                }
                if (!net.ready(frame)) {
                    ++stalls;
                    acc = 0;  // wait for the other players
                    break;
                }
                for (int i = 0; i < net.players(); ++i) consoles[size_t(i)]->set_keys(net.input(i, frame));
                cable->run_frame();
                ++frame;
                if (opt.quit_frame > 0 && long(frame) >= opt.quit_frame) {
                    running = false;
                    break;
                }
                acc -= kFrameSeconds;
                for (int i = 0; i < net.players(); ++i) {
                    if (i == net.local_slot()) consoles[size_t(i)]->take_audio(audio_buf);
                    else consoles[size_t(i)]->take_audio(discard);
                }
                if (audio && !audio_buf.empty()) {
                    if (SDL_GetAudioStreamQueued(audio) < kamo::gba::kAudioRate / 5 * 4)
                        SDL_PutAudioStreamData(audio, audio_buf.data(), int(audio_buf.size() * sizeof(std::int16_t)));
                    audio_buf.clear();
                }
                discard.clear();
                if (frame % 60 == 0) net.submit_hash(frame, state_hash(consoles));
            }
        }

        // Window title: status line.
        std::string title = "KAMO Netplay - ";
        if (st == kamo::net::State::Running) {
            title += "Player " + std::to_string(net.local_slot() + 1) + "/" + std::to_string(net.players()) +
                     " - ping " + std::to_string(net.ping_ms()) + " ms - delay " + std::to_string(net.delay()) +
                     " - frame " + std::to_string(frame) + " - waits " + std::to_string(stalls);
            if (net.desync_frame() >= 0) title += " - DESYNC at frame " + std::to_string(net.desync_frame());
        } else {
            title += net.message();
        }
        if (title != last_title) {
            if (window) SDL_SetWindowTitle(window, title.c_str());
            last_title = title;
        }
        // Console log: status changes before the game starts, and the first desync.
        if (st != kamo::net::State::Running && net.message() != last_message) {
            std::cout << net.message() << "\n";
            last_message = net.message();
        }
        if (net.desync_frame() >= 0 && !desync_reported) {
            std::cout << "DESYNC: the consoles differ from frame " << net.desync_frame()
                      << ". Please report this with both players' KAMO versions.\n";
            desync_reported = true;
        }
        if (st == kamo::net::State::Failed) {
            std::cout << "\n" << net.message() << "\n";
            running = false;
        }

        // Draw this player's console (black while waiting).
        if (!renderer) { SDL_Delay(1); continue; }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (!consoles.empty()) {
            SDL_UpdateTexture(tex, nullptr, consoles[size_t(net.local_slot())]->framebuffer(),
                              kamo::gba::kScreenWidth * 2);
            SDL_RenderTexture(renderer, tex, nullptr, nullptr);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    if (!consoles.empty()) {
        std::printf("final frame %u hash %016llx\n", frame, (unsigned long long)state_hash(consoles));
        if (!opt.dump.empty()) kamo::write_bmp(opt.dump.string(), consoles[size_t(net.local_slot())]->framebuffer());
    }
    net.stop();
    cable.reset();
    consoles.clear();
    if (pad) SDL_CloseGamepad(pad);
    if (audio) SDL_DestroyAudioStream(audio);
    if (tex) SDL_DestroyTexture(tex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    if (net.state() == kamo::net::State::Failed) ask("Press Enter to close");
    return 0;
}
