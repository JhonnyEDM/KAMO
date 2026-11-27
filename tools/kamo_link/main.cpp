// kamo_link: runs 2-4 consoles of the game in one process, joined by the virtual link cable,
// without a window. For developing and testing local multiplayer (stage 2).
//
//   kamo_link --rom <file.gba> --units 2 --frames 3000 --out <dir>
//             [--input 0:900:8,906:0] [--input 1:900:8,906:0] [--dump-at 1200,2400]
//
// --input  <unit>:<frame>:<hex keys>[,<frame>:<hex keys>...]   scripted buttons for one console
// --dump-at <frames>  extra frames to save as BMP (unitN-fFRAME.bmp); the last frame is always saved
// Saves go to <dir>/unitN.sav, so the user's own saves are never touched.
// Everything written is derived from the ROM: keep <dir> out of the repository.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/bmp.h"
#include "gba/gba.h"
#include "gba/link.h"
#include "rom.h"

namespace fs = std::filesystem;
using kamo::gba::Gba;
using kamo::gba::LinkCable;
using kamo::gba::u64;

namespace {

struct Script {
    std::vector<std::pair<long, std::uint16_t>> events;
    size_t next = 0;
};

bool parse_script(const std::string& s, Script& out) {
    size_t p = 0;
    while (p < s.size()) {
        size_t comma = s.find(',', p);
        std::string item = s.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
        size_t colon = item.find(':');
        if (colon == std::string::npos) return false;
        out.events.emplace_back(std::strtol(item.c_str(), nullptr, 10),
                                std::uint16_t(std::strtoul(item.c_str() + colon + 1, nullptr, 16)));
        if (comma == std::string::npos) break;
        p = comma + 1;
    }
    return true;
}

int usage() {
    std::fprintf(stderr,
                 "usage: kamo_link --rom <file.gba> --units <2-4> --frames <n> --out <dir>\n"
                 "                 [--input <unit>:<frame>:<hexkeys>,...] [--dump-at <f1,f2,...>]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path rom_path, out;
    int units = 2;
    long frames = 0;
    std::vector<long> dump_at;
    long trace_from = -1;
    int cpu_pct = 0;  // 0 = the cable default (LinkCable::kLinkCpuSpeedPct)
    std::vector<Script> scripts(4);
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") rom_path = next();
        else if (a == "--units") units = std::atoi(next().c_str());
        else if (a == "--frames") frames = std::strtol(next().c_str(), nullptr, 10);
        else if (a == "--out") out = next();
        else if (a == "--input") {
            std::string s = next();
            size_t colon = s.find(':');
            int u = std::atoi(s.c_str());
            if (colon == std::string::npos || u < 0 || u > 3 || !parse_script(s.substr(colon + 1), scripts[size_t(u)]))
                return usage();
        } else if (a == "--dump-at") {
            std::string s = next();
            for (size_t p = 0; p <= s.size();) {
                dump_at.push_back(std::strtol(s.c_str() + p, nullptr, 10));
                size_t c = s.find(',', p);
                if (c == std::string::npos) break;
                p = c + 1;
            }
        } else if (a == "--trace-from") trace_from = std::strtol(next().c_str(), nullptr, 10);
        else if (a == "--cpu-pct") cpu_pct = std::atoi(next().c_str());
        else return usage();
    }
    if (rom_path.empty() || out.empty() || frames <= 0 || units < 2 || units > 4) return usage();

    auto loaded = kamo::load_rom(rom_path);
    if (!loaded.ok || !loaded.rom.hash_matches) {
        std::fprintf(stderr, "ROM not accepted: %s\n", loaded.ok ? "not the supported dump" : loaded.error.c_str());
        return 1;
    }
    fs::create_directories(out);

    std::vector<std::unique_ptr<Gba>> consoles;
    std::vector<Gba*> ptrs;
    for (int u = 0; u < units; ++u) {
        consoles.push_back(std::make_unique<Gba>(loaded.rom.data, out / ("unit" + std::to_string(u) + ".sav")));
        consoles.back()->reset();

        ptrs.push_back(consoles.back().get());
    }
    LinkCable cable(ptrs);
    if (cpu_pct > 0)
        for (auto& c : consoles) c->cpu_speed_pct = kamo::gba::u32(cpu_pct);

    auto dump = [&](long f, bool last) {
        for (int u = 0; u < units; ++u) {
            std::string name = last ? "unit" + std::to_string(u) + ".bmp"
                                    : "unit" + std::to_string(u) + "-f" + std::to_string(f) + ".bmp";
            kamo::write_bmp((out / name).string(), consoles[size_t(u)]->framebuffer());
        }
    };

    u64 last_transfers = 0;
    for (long f = 0; f < frames; ++f) {
        for (int u = 0; u < units; ++u) {
            Script& s = scripts[size_t(u)];
            while (s.next < s.events.size() && s.events[s.next].first <= f)
                consoles[size_t(u)]->set_keys(s.events[s.next++].second);
        }
        if (f == trace_from)
            for (auto& c : consoles) c->sio_trace = true;
        cable.run_frame();
        for (long d : dump_at)
            if (d == f + 1) dump(f + 1, false);
        if ((f + 1) % 60 == 0) {
            std::printf("frame %5ld  dropped %llu  transfers %8llu (+%llu)  mode:", f + 1, (unsigned long long)cable.dropped(), (unsigned long long)cable.transfers(),
                        (unsigned long long)(cable.transfers() - last_transfers));
            for (int u = 0; u < units; ++u)
                std::printf(" u%d=%s siocnt=%04X", u, consoles[size_t(u)]->sio_multiplayer_mode() ? "MP" : "--",
                            consoles[size_t(u)]->sio_control());
            std::printf("\n");
            last_transfers = cable.transfers();
        }
    }
    dump(frames, true);
    std::printf("done: %ld frames, %llu transfers\n", frames, (unsigned long long)cable.transfers());
    return 0;
}
