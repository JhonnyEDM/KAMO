#pragma once
// Online looks for the four Kirbys: each player's own colour and their name above their Kirby.
//
// Presentation only. In Kirby & The Amazing Mirror, cable port N draws its Kirby with OBJ
// palette bank N and tiles 64*N .. 64*N+63. KAMO reads the local console's palette RAM and OAM
// before each presented frame, redraws those banks through the PPU's presentation-only palette
// override and places the names from the sprites. Guest memory, save states and the netplay
// state never change, so every PC stays in sync whatever colours it shows.

#include <array>
#include <cstdint>
#include <string>

namespace gbarecomp { struct GbaNetplayLaunch; }

namespace kamo {

constexpr int kKirbyPorts = 4;

struct KirbyLooks {
    std::array<std::string, kKirbyPorts> names;    // by cable port; empty = no label
    std::array<std::uint32_t, kKirbyPorts> colors{}; // 0xRRGGBB, or kOriginalColor
    bool show_names = true;
};
constexpr std::uint32_t kOriginalColor = 0xFFFFFFFFu;

// Hooks the looks into an online launch (before_present + host_overlay).
void install_kirby_looks(gbarecomp::GbaNetplayLaunch& launch, const KirbyLooks& looks);

// A Kirby palette bank (16 BGR555 colours) redrawn around `rgb`: body ramp (2-8) and feet (9-11)
// take its hue and saturation and keep their shading; outline, eyes and shine stay.
std::array<std::uint16_t, 16> recolor_kirby(const std::uint16_t* original, std::uint32_t rgb);
// True for a bank laid out like a Kirby palette (dark outline, ivory-to-white shine at 12-15).
bool looks_like_kirby_bank(const std::uint16_t* bank);

// "RRGGBB" <-> colour; "" or "original" is kOriginalColor.
std::uint32_t parse_kirby_color(const std::string& text);
std::string kirby_color_text(std::uint32_t color);

}  // namespace kamo
