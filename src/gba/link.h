#pragma once
// Virtual link cable: connects 2-4 Gba instances in the same process in SIO multiplayer mode
// (the mode Kirby & The Amazing Mirror uses for its co-op). The cable never touches the
// network: in online play every PC runs all four consoles and only button input travels
// (see docs/MULTIPLAYER.md).
//
// Unit 0 is the parent (player 1). A transfer starts when the parent sets SIOCNT bit 7 in
// multiplayer mode. After the transfer time for the selected baud rate, every unit receives
// all units' SIOMLT_SEND values in SIOMULTI0-3 (0xFFFF for empty slots), the busy bit clears and
// units with SIOCNT bit 14 set get the serial interrupt.
//
// The consoles run interleaved in short slices so their clocks stay within a few hundred cycles
// of each other. Everything is deterministic: same inputs, same result.

#include <array>
#include <functional>
#include <vector>

#include "gba/gba.h"

namespace kamo::gba {

class LinkCable {
public:
    // Slice length in CPU cycles between switching consoles.
    static constexpr u32 kSliceCycles = 128;
    // CPU speed given to linked consoles (Gba::cpu_speed_pct). At 100 % a child console fell
    // behind during heavy frames and the game reported "Communication Error!". A recorded online
    // session failed at 125 % and 150 % (player 1 going through a door while player 2 fought) and
    // passed from 175 %; 200 % leaves margin. Remove once instruction timing matches hardware.
    static constexpr u32 kLinkCpuSpeedPct = 200;

    explicit LinkCable(std::vector<Gba*> units);
    ~LinkCable();
    LinkCable(const LinkCable&) = delete;
    LinkCable& operator=(const LinkCable&) = delete;

    // Runs every console until each has completed one frame.
    void run_frame();
    // Debug: called after every slice with the console index (profiling tools).
    std::function<void(int, const Gba&)> on_slice;

    int size() const { return int(units_.size()); }
    // Completed transfers since creation (diagnostics).
    u64 transfers() const { return transfers_; }
    // Start requests ignored because a transfer was still in flight (diagnostics).
    u64 dropped() const { return dropped_; }

    // Called by the parent Gba.
    void start_transfer();
    void complete_transfer();
    bool all_in_multiplayer_mode() const;

private:
    std::vector<Gba*> units_;
    std::array<u16, 4> latched_{};  // SIOMLT_SEND of each console at transfer start
    u64 transfers_ = 0;
    u64 dropped_ = 0;
};

}  // namespace kamo::gba
