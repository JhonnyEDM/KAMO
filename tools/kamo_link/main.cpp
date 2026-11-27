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

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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
    std::vector<std::vector<std::uint16_t>> replay;  // per frame, per player (from --replay)
    bool quiet = false;
    long prof_from = -1, prof_to = -1;
    std::array<std::map<std::uint32_t, long>, 4> prof_blocked, prof_all;
    long cur_frame = 0;
    bool expect_link = false;  // exit 1 if the link stops (regression tests)
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
        else if (a == "--quiet") quiet = true;
        else if (a == "--profile") {
            // "<from>-<to>": per console, where the CPU spends slices with IRQs blocked.
            std::string s = next();
            prof_from = std::strtol(s.c_str(), nullptr, 10);
            size_t dash = s.find('-');
            prof_to = dash == std::string::npos ? prof_from + 60 : std::strtol(s.c_str() + dash + 1, nullptr, 10);
        }
        else if (a == "--expect-link") expect_link = true;
        else if (a == "--replay") {
            // A KAMO-Netplay recording: header lines, then "frames", then one line per frame.
            std::ifstream in(next());
            std::string line;
            bool body = false;
            while (std::getline(in, line)) {
                if (!body) {
                    if (line.rfind("players ", 0) == 0) units = std::atoi(line.c_str() + 8);
                    body = line == "frames";
                    continue;
                }
                std::vector<std::uint16_t> keys;
                for (size_t p = 0; p < line.size();) {
                    char* end = nullptr;
                    keys.push_back(std::uint16_t(std::strtoul(line.c_str() + p, &end, 16)));
                    p = size_t(end - line.c_str());
                    while (p < line.size() && line[p] == ' ') ++p;
                    if (end == line.c_str()) break;
                }
                replay.push_back(std::move(keys));
            }
            if (replay.empty()) return usage();
        }
        else return usage();
    }
    if (!replay.empty() && frames <= 0) frames = long(replay.size());
    if (rom_path.empty()) {
        // Tests: take the ROM from KAMO_ROM, and skip (exit 77) when there is none.
        const char* env = std::getenv("KAMO_ROM");
        if (!env || !*env) {
            std::printf("skipped: ROM not available (set KAMO_ROM)\n");
            return 77;
        }
        rom_path = env;
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
    cable.on_slice = [&](int u, const Gba& g) {
        if (cur_frame < prof_from || cur_frame >= prof_to) return;
        const std::uint32_t block = g.debug_pc() & ~0xFFu;
        ++prof_all[size_t(u)][block];
        if (g.debug_irq_blocked()) ++prof_blocked[size_t(u)][block];
    };
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
    u64 prev_transfers = 0;
    int still = 0;
    long stall_reported = -1;
    for (long f = 0; f < frames; ++f) {
        for (int u = 0; u < units; ++u) {
            Script& s = scripts[size_t(u)];
            while (s.next < s.events.size() && s.events[s.next].first <= f)
                consoles[size_t(u)]->set_keys(s.events[s.next++].second);
        }
        if (f < long(replay.size()))
            for (int u = 0; u < units && u < int(replay[size_t(f)].size()); ++u)
                consoles[size_t(u)]->set_keys(replay[size_t(f)][size_t(u)]);
        cur_frame = f;
        if (f == trace_from)
            for (auto& c : consoles) c->sio_trace = true;
        cable.run_frame();
        for (long d : dump_at)
            if (d == f + 1) dump(f + 1, false);
        // The link stalls when a console misses its slot ("Communication Error!" in the game).
        if (stall_reported < 0 && cable.transfers() > 0 && cable.transfers() == prev_transfers) {
            if (++still >= 30) {
                std::printf("LINK STOPPED at frame %ld (no transfers for 30 frames)\n", f + 1 - 30);
                stall_reported = f + 1 - 30;
            }
        } else {
            still = 0;
        }
        prev_transfers = cable.transfers();
        if (!quiet && (f + 1) % 60 == 0) {
            std::printf("frame %5ld  dropped %llu  transfers %8llu (+%llu)  mode:", f + 1, (unsigned long long)cable.dropped(), (unsigned long long)cable.transfers(),
                        (unsigned long long)(cable.transfers() - last_transfers));
            for (int u = 0; u < units; ++u)
                std::printf(" u%d=%s siocnt=%04X", u, consoles[size_t(u)]->sio_multiplayer_mode() ? "MP" : "--",
                            consoles[size_t(u)]->sio_control());
            std::printf("\n");
            last_transfers = cable.transfers();
        }
    }
    if (prof_from >= 0) {
        for (int u = 0; u < units; ++u) {
            std::vector<std::pair<long, std::uint32_t>> top;
            long total = 0, blocked = 0;
            for (auto& [b, n] : prof_all[size_t(u)]) total += n;
            for (auto& [b, n] : prof_blocked[size_t(u)]) { top.emplace_back(n, b); blocked += n; }
            std::sort(top.rbegin(), top.rend());
            std::printf("unit %d frames %ld-%ld: %ld slices, %ld with IRQs blocked\n", u, prof_from, prof_to, total, blocked);
            for (size_t k = 0; k < top.size() && k < 8; ++k)
                std::printf("   %08X..%08X  %ld blocked slices\n", top[k].second, top[k].second + 0xFF, top[k].first);
        }
    }
    dump(frames, true);
    std::printf("done: %ld frames, %llu transfers\n", frames, (unsigned long long)cable.transfers());
    if (expect_link && stall_reported >= 0) return 1;
    return 0;
}
