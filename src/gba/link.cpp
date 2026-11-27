// Virtual link cable (SIO multiplayer mode). See link.h.
#include "gba/link.h"

#include <array>

namespace kamo::gba {

namespace {
// Bits per unit on the wire: start bit, 16 data bits, stop bit.
constexpr u64 kBitsPerUnit = 18;
constexpr std::array<u64, 4> kBaud = {9600, 38400, 57600, 115200};
}  // namespace

LinkCable::LinkCable(std::vector<Gba*> units) : units_(std::move(units)) {
    for (size_t i = 0; i < units_.size(); ++i) {
        units_[i]->attach_link(this, int(i));
        units_[i]->cpu_speed_pct = kLinkCpuSpeedPct;
    }
}

LinkCable::~LinkCable() {
    for (Gba* g : units_) {
        g->attach_link(nullptr, 0);
        g->cpu_speed_pct = 100;
    }
}

void LinkCable::run_frame() {
    std::vector<bool> done(units_.size(), false);
    size_t remaining = units_.size();
    while (remaining > 0) {
        for (size_t i = 0; i < units_.size(); ++i) {
            if (done[i]) continue;
            if (units_[i]->run_slice(kSliceCycles)) {
                done[i] = true;
                --remaining;
            }
        }
    }
}

bool LinkCable::all_in_multiplayer_mode() const {
    for (const Gba* g : units_)
        if (!g->sio_multiplayer_mode()) return false;
    return units_.size() >= 2;
}

void LinkCable::start_transfer() {
    Gba& parent = *units_[0];
    if (parent.sio_busy_) { ++dropped_; return; }
    const u16 siocnt = parent.sio_control();
    const u64 baud = kBaud[siocnt & 3];
    parent.sio_busy_ = true;
    parent.sio_remaining_ = s64(u64(kCpuHz) * kBitsPerUnit * units_.size() / baud);
    // Each console's SIOMLT_SEND is latched when the transfer starts.
    latched_ = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    for (size_t i = 0; i < units_.size() && i < 4; ++i)
        if (units_[i]->sio_multiplayer_mode()) latched_[i] = units_[i]->sio_send_value();
    // Every connected console shows the transfer as busy until it completes.
    for (Gba* g : units_)
        if (g->sio_multiplayer_mode()) g->sio_set_busy(true);
}

void LinkCable::complete_transfer() {
    Gba& parent = *units_[0];
    parent.sio_busy_ = false;
    const std::array<u16, 4> data = latched_;
    for (size_t i = 0; i < units_.size(); ++i) {
        Gba& g = *units_[i];
        if (!g.sio_multiplayer_mode()) continue;
        g.sio_receive(data, int(i));
    }
    ++transfers_;
}

}  // namespace kamo::gba
