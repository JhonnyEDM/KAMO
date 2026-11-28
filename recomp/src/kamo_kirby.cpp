#include "kamo_kirby.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "host_overlay.h"
#include "multiplayer_launch.h"
#include "multiplayer_session.h"

namespace kamo {
namespace {

struct Hsl { float h = 0, s = 0, l = 0; };

Hsl to_hsl(float r, float g, float b) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    Hsl o;
    o.l = (mx + mn) / 2;
    const float d = mx - mn;
    if (d < 1e-6f) return o;
    o.s = o.l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) o.h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) o.h = (b - r) / d + 2;
    else o.h = (r - g) / d + 4;
    o.h /= 6;
    return o;
}

float hue_part(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

std::uint16_t from_hsl(Hsl c) {
    float r = c.l, g = c.l, b = c.l;
    if (c.s > 1e-6f) {
        const float q = c.l < 0.5f ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s;
        const float p = 2 * c.l - q;
        r = hue_part(p, q, c.h + 1.0f / 3);
        g = hue_part(p, q, c.h);
        b = hue_part(p, q, c.h - 1.0f / 3);
    }
    auto five = [](float v) { return std::uint16_t(std::clamp(int(std::lround(v * 31)), 0, 31)); };
    return std::uint16_t(five(r) | (five(g) << 5) | (five(b) << 10));
}

Hsl hsl555(std::uint16_t c) {
    return to_hsl((c & 31) / 31.0f, ((c >> 5) & 31) / 31.0f, ((c >> 10) & 31) / 31.0f);
}

Hsl hsl888(std::uint32_t rgb) {
    return to_hsl(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f);
}

// Per-frame state, owned by the netplay thread (before_present and the overlay run on it).
struct Tag { bool visible = false; float x = 0, y = 0; };
struct State {
    KirbyLooks looks;
    std::array<Tag, kKirbyPorts> tags;
    std::array<std::array<std::uint16_t, 16>, kKirbyPorts> source{}, drawn{};
    std::array<bool, kKirbyPorts> overridden{};
} g_state;

void before_present(gbarecomp::GbaInstance& machine) {
    auto& bus = machine.bus;
    for (int port = 0; port < kKirbyPorts; ++port) {
        std::array<std::uint16_t, 16> bank{};
        for (int i = 0; i < 16; ++i) bank[i] = bus.read16(0x05000200u + port * 32 + i * 2);
        const bool kirby = looks_like_kirby_bank(bank.data());
        const std::uint32_t color = g_state.looks.colors[port];
        if (color == kOriginalColor || !kirby) {
            if (g_state.overridden[port]) { machine.ppu.set_obj_palette_override(port, nullptr); g_state.overridden[port] = false; }
        } else {
            if (!g_state.overridden[port] || bank != g_state.source[port]) {
                g_state.source[port] = bank;
                g_state.drawn[port] = recolor_kirby(bank.data(), color);
                machine.ppu.set_obj_palette_override(port, g_state.drawn[port].data());
                g_state.overridden[port] = true;
            }
        }
        g_state.tags[port].visible = false;
        if (!g_state.looks.show_names || g_state.looks.names[port].empty() || !kirby) continue;
        // The port's Kirby: sprites in its palette bank whose tiles are in its 64-tile block.
        static const int kSize[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                                           {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                                           {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
        float left = 1e9f, right = -1e9f, top = 1e9f;
        for (int i = 0; i < 128; ++i) {
            const std::uint16_t a0 = bus.read16(0x07000000u + i * 8);
            const std::uint16_t a1 = bus.read16(0x07000000u + i * 8 + 2);
            const std::uint16_t a2 = bus.read16(0x07000000u + i * 8 + 4);
            const int mode = (a0 >> 8) & 3, shape = (a0 >> 14) & 3;
            if (mode == 2 || shape == 3) continue;                     // hidden / invalid
            if ((a2 >> 12) != port || ((a0 >> 13) & 1)) continue;      // other bank / 256 colours
            const int tile = a2 & 1023;
            if (tile < port * 64 || tile >= port * 64 + 64) continue;  // effects, not the body
            int x = a1 & 511, y = a0 & 255;
            if (x >= 256) x -= 512;
            if (y >= 160) y -= 256;
            const int w = kSize[shape][(a1 >> 14) & 3][0];
            left = std::min(left, float(x));
            right = std::max(right, float(x + w));
            top = std::min(top, float(y));
        }
        if (right > left && top < 160 && right > 0 && left < 240)
            g_state.tags[port] = {true, (left + right) / 2, top};
    }
}

void draw_tags(gbarecomp::HostOverlay* ov) {
    if (!ov->text_supported()) return;
    const auto rect = ov->game_rect();
    if (rect.width <= 0 || rect.height <= 0) return;
    const float px = rect.height / 160.0f;  // one GBA pixel in drawable pixels
    const float size = std::max(10.0f, 6.5f * px), pad = std::max(2.0f, 1.2f * px);
    const float h = size + pad * 2;
    // Place every label above its Kirby, left to right; one that would cover a label already
    // placed moves up a row, so Kirbys standing together keep readable names.
    struct Placed { int port; float x, y, w; };
    std::array<Placed, kKirbyPorts> placed{};
    int count = 0;
    std::array<int, kKirbyPorts> order{0, 1, 2, 3};
    std::sort(order.begin(), order.end(), [](int a, int b) { return g_state.tags[a].x < g_state.tags[b].x; });
    for (const int port : order) {
        const auto& tag = g_state.tags[port];
        if (!tag.visible) continue;
        const char* name = g_state.looks.names[port].c_str();
        const float w = ov->text_width(name, size, gbarecomp::OverlaySpace::Drawable) + pad * 2;
        const float cx = rect.x + tag.x * rect.width / 240.0f;
        const float x = std::clamp(cx - w / 2, float(rect.x) + 1, float(rect.x + rect.width) - w - 1);
        float y = rect.y + tag.y * px - h - 2 * px;
        for (bool moved = true; moved;) {
            moved = false;
            for (int i = 0; i < count; ++i) {
                const auto& o = placed[i];
                if (x < o.x + o.w + 2 && o.x < x + w + 2 && y < o.y + h + 2 && o.y < y + h + 2) {
                    y = o.y - h - 2;
                    moved = true;
                }
            }
        }
        placed[count++] = {port, x, y, w};
    }
    for (int i = 0; i < count; ++i) {
        const int port = placed[i].port;
        const char* name = g_state.looks.names[port].c_str();
        const float w = placed[i].w, x = placed[i].x;
        const float y = std::max(float(rect.y) + 1, placed[i].y);
        std::uint32_t c = g_state.looks.colors[port];
        if (c == kOriginalColor) {
            static const std::uint32_t kPortColor[kKirbyPorts] = {0xFF8CC8, 0xFFE040, 0xFF4060, 0x70E050};
            c = kPortColor[port];
        }
        const gbarecomp::OverlayColor accent{std::uint8_t(c >> 16), std::uint8_t(c >> 8), std::uint8_t(c), 255};
        ov->fill_rect(x, y, w, h, pad, {20, 12, 38, 200}, gbarecomp::OverlaySpace::Drawable);
        ov->fill_rect(x, y + h - std::max(1.5f, px * 0.6f), w, std::max(1.5f, px * 0.6f), 0, accent,
                      gbarecomp::OverlaySpace::Drawable);
        ov->text(x + pad, y + pad, size, {255, 249, 223, 255}, name, gbarecomp::OverlayAlign::Left,
                 gbarecomp::OverlaySpace::Drawable);
    }
}

}  // namespace

bool looks_like_kirby_bank(const std::uint16_t* bank) {
    const auto dark = [](std::uint16_t c) { return (c & 31) <= 4 && ((c >> 5) & 31) <= 4 && ((c >> 10) & 31) <= 4; };
    const auto bright = [](std::uint16_t c) { return (c & 31) >= 26 && ((c >> 5) & 31) >= 26 && ((c >> 10) & 31) >= 20; };
    return dark(bank[1]) && bright(bank[14]) && (bank[15] & 0x7FFF) == 0x7FFF && hsl555(bank[4]).s > 0.3f;
}

std::array<std::uint16_t, 16> recolor_kirby(const std::uint16_t* original, std::uint32_t rgb) {
    std::array<std::uint16_t, 16> out{};
    std::copy(original, original + 16, out.begin());
    const Hsl target = hsl888(rgb);
    const Hsl mid = hsl555(original[4]);         // the body's middle tone
    const float shift = target.l - mid.l;
    for (int i = 2; i <= 8; ++i) {              // body, light to dark
        Hsl c = hsl555(original[i]);
        c.h = target.h;
        c.s = target.s;
        c.l = std::clamp(c.l + shift, 0.04f, 0.97f);
        out[i] = from_hsl(c);
    }
    for (int i = 9; i <= 11; ++i) {             // feet: the same hue, deeper
        Hsl c = hsl555(original[i]);
        c.h = target.h;
        c.s = target.s < 0.15f ? target.s : std::min(1.0f, target.s * 0.9f + 0.15f);
        c.l = std::clamp(c.l + shift * 0.6f - 0.06f, 0.06f, 0.85f);
        out[i] = from_hsl(c);
    }
    return out;
}

std::uint32_t parse_kirby_color(const std::string& text) {
    if (text.size() != 6) return kOriginalColor;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &end, 16);
    return end && *end == '\0' ? std::uint32_t(v & 0xFFFFFF) : kOriginalColor;
}

std::string kirby_color_text(std::uint32_t color) {
    if (color == kOriginalColor) return "original";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06X", unsigned(color & 0xFFFFFF));
    return buf;
}

void install_kirby_looks(gbarecomp::GbaNetplayLaunch& launch, const KirbyLooks& looks) {
    g_state = State{};
    g_state.looks = looks;
    launch.before_present = before_present;
    launch.host_overlay = draw_tags;
}

}  // namespace kamo
