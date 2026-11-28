// KAMO's menu window: first-run ROM setup, Play alone, Play online (rooms on the public lobby
// server through recomp-ui's lobby backend), Options and automatic updates. Drawn with the
// Dear ImGui copy that recomp-ui already builds into KAMO.

#include "kamo_frontend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "tinyfiledialogs.h"

#include "recomp_launcher.h"
#include "launcher_netplay_seam.h"

#include "kamo_game.h"
#include "kamo_settings.h"
#include "kamo_update.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#endif

#ifndef KAMO_VERSION
#define KAMO_VERSION "0.0.0"
#endif

namespace kamo {
namespace {

// ---- language ----------------------------------------------------------------------------
bool g_spanish = false;
const char* T(const char* es, const char* en) { return g_spanish ? es : en; }

void detect_language() {
#if defined(_WIN32)
    g_spanish = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH;
#endif
}

// ---- look -------------------------------------------------------------------------------
constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
const ImVec4 kBackground = rgb(255, 232, 241);
const ImVec4 kInk = rgb(84, 32, 70);
const ImVec4 kSoftInk = rgb(150, 92, 128);
const ImVec4 kPink = rgb(236, 72, 132);
const ImVec4 kPinkHover = rgb(244, 104, 156);
const ImVec4 kPinkActive = rgb(206, 46, 108);
const ImVec4 kCard = rgb(255, 247, 251);
const ImVec4 kGood = rgb(46, 160, 98);
const ImVec4 kBad = rgb(214, 54, 72);

void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 14; s.GrabRounding = 14; s.ChildRounding = 16;
    s.PopupRounding = 14; s.ScrollbarRounding = 12; s.TabRounding = 12;
    s.FramePadding = ImVec2(14, 9); s.ItemSpacing = ImVec2(12, 12); s.WindowPadding = ImVec2(28, 22);
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = 0; s.FrameBorderSize = 0;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kInk;
    c[ImGuiCol_TextDisabled] = kSoftInk;
    c[ImGuiCol_WindowBg] = kBackground;
    c[ImGuiCol_ChildBg] = kCard;
    c[ImGuiCol_PopupBg] = kCard;
    c[ImGuiCol_ModalWindowDimBg] = rgb(84, 32, 70, 0.35f);
    c[ImGuiCol_FrameBg] = rgb(255, 255, 255);
    c[ImGuiCol_FrameBgHovered] = rgb(255, 240, 246);
    c[ImGuiCol_FrameBgActive] = rgb(255, 228, 239);
    c[ImGuiCol_Button] = kPink;
    c[ImGuiCol_ButtonHovered] = kPinkHover;
    c[ImGuiCol_ButtonActive] = kPinkActive;
    c[ImGuiCol_CheckMark] = kPink;
    c[ImGuiCol_SliderGrab] = kPink;
    c[ImGuiCol_SliderGrabActive] = kPinkActive;
    c[ImGuiCol_Header] = rgb(255, 214, 231);
    c[ImGuiCol_HeaderHovered] = rgb(255, 200, 222);
    c[ImGuiCol_HeaderActive] = rgb(255, 186, 214);
    c[ImGuiCol_TableHeaderBg] = rgb(255, 214, 231);
    c[ImGuiCol_TableRowBg] = rgb(255, 255, 255, 0);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 236, 244);
    c[ImGuiCol_TableBorderLight] = rgb(255, 214, 231);
    c[ImGuiCol_TableBorderStrong] = rgb(255, 214, 231);
    c[ImGuiCol_Separator] = rgb(246, 190, 214);
    c[ImGuiCol_NavHighlight] = rgb(120, 60, 200);
    c[ImGuiCol_ScrollbarBg] = rgb(255, 255, 255, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(246, 170, 204);
}

struct Texture { GLuint id = 0; int w = 0, h = 0; };

Texture load_png(const std::filesystem::path& path) {
    Texture t;
    int n = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &t.w, &t.h, &n, 4);
    if (!pixels) return {};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return t;
}

void set_window_icon(SDL_Window* window, const std::filesystem::path& path) {
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &n, 4);
    if (!pixels) return;
    if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w * 4,
                                                                SDL_PIXELFORMAT_RGBA32)) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
    stbi_image_free(pixels);
}

// ---- small widgets ----------------------------------------------------------------------
ImFont* g_title_font = nullptr;
ImFont* g_big_font = nullptr;

float center_x(float width) { return std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f); }

bool big_button(const char* label, float width = 340, bool enabled = true) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(width));
    ImGui::BeginDisabled(!enabled);
    ImGui::PushFont(g_big_font);
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 28);
    const bool pressed = ImGui::Button(label, ImVec2(width, 58));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndDisabled();
    return pressed;
}

bool button(const char* label, float width = 0, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(255, 255, 255));
    const bool pressed = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    return pressed;
}

bool soft_button(const char* label, float width = 0) {
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(255, 214, 231));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(255, 196, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(255, 178, 208));
    const bool pressed = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleColor(3);
    return pressed;
}

void centered_text(const char* text, ImVec4 color = kInk, float wrap = 620) {
    const float width = std::min(wrap, ImGui::CalcTextSize(text, nullptr, false, wrap).x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(width));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void heading(const char* text) {
    ImGui::PushFont(g_title_font);
    centered_text(text, kPinkActive);
    ImGui::PopFont();
    ImGui::Spacing();
}

// ---- network helpers --------------------------------------------------------------------
int free_udp_port(int preferred) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    for (int port = preferred; port < preferred + 32; ++port) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) break;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        const bool ok = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        closesocket(s);
        if (ok) { WSACleanup(); return port; }
    }
    WSACleanup();
#endif
    return preferred;
}

// Input delay from the slowest round trip in the room: enough frames for input to arrive
// before it is needed, without adding lag on a good connection.
int delay_from_ping(int rtt_ms) {
    if (rtt_ms < 0) return 4;
    if (rtt_ms < 40) return 2;
    if (rtt_ms < 80) return 3;
    if (rtt_ms < 130) return 4;
    if (rtt_ms < 190) return 5;
    if (rtt_ms < 260) return 6;
    return 8;
}

// ---- the menu ---------------------------------------------------------------------------
enum class Screen { Setup, Menu, Online, Create, Room, Options };

struct Front {
    std::filesystem::path dir;
    gbarecomp::RunOptions* options = nullptr;
    Settings settings;
    KeyBinding keys[kButtonCount];
    std::filesystem::path rom;
    Screen screen = Screen::Menu;
    std::string message;        // a short note under the current screen
    bool message_bad = false;
    Updater updater;
    Texture logo;
    bool start_game = false;
    bool quit = false;
    int capture_key = -1;       // index into keys while waiting for a key press

    // online
    const RecompLauncherCNetplayCallbacks* np = nullptr;
    std::string lobby_version;
    Uint32 next_list = 0;
    char name[25] = {};
    char room_name[64] = {};
    char password[64] = {};
    char join_password[64] = {};
    bool room_private = false;
    int room_players = 4;
    std::string joining_id;
    Uint32 bar_since = 0;       // update bar: when it slid in
    Uint32 bar_hide_at = 0;     // update bar: when it starts sliding away (0 = stays)
    Uint32 ready_since = 0;     // the installer checked out at this time
    bool auto_update = false;   // the download started by itself once
    bool install_failed = false;
    Uint32 waiting_since = 0;   // when the Room screen began waiting for the server
    bool create_sent = false;   // a create is on its way: the room exists before in_lobby says so
    bool continue_saved = false;
    std::string room_title;
    char chat_line[200] = {};
    std::uint32_t chat_seen = 0;   // seq of the newest chat line shown, to scroll to new ones
};

void note(Front& f, std::string text, bool bad = false) { f.message = std::move(text); f.message_bad = bad; }

void draw_message(Front& f) {
    if (f.message.empty()) return;
    ImGui::Spacing();
    centered_text(f.message.c_str(), f.message_bad ? kBad : kGood);
}

void pick_rom(Front& f) {
    const char* patterns[] = {"*.gba", "*.GBA"};
    const char* picked = tinyfd_openFileDialog(T("Elige tu ROM de Kirby & The Amazing Mirror",
                                                 "Choose your Kirby & The Amazing Mirror ROM"),
                                               "", 2, patterns, "GBA ROM (*.gba)", 0);
    if (!picked) return;
    const auto path = std::filesystem::u8path(picked);
    const auto problem = check_rom(path);
    if (problem.empty()) {
        f.rom = path;
        save_rom_path(f.dir, path);
        note(f, T("ROM verificada. ¡Todo listo!", "ROM verified. All set!"));
        if (f.screen == Screen::Setup) f.screen = Screen::Menu;
    } else if (problem == "wrong-rom") {
        note(f, T("Ese archivo no es Kirby & The Amazing Mirror (USA). KAMO solo funciona con esa versión.",
                  "That file is not Kirby & The Amazing Mirror (USA). KAMO only works with that version."), true);
    } else {
        note(f, T("No se pudo leer ese archivo.", "That file could not be read."), true);
    }
}

void draw_logo(Front& f, float max_width) {
    if (!f.logo.id) { heading("KAMO"); return; }
    const float w = std::min(max_width, float(f.logo.w));
    const float h = w * f.logo.h / f.logo.w;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(w));
    ImGui::Image(static_cast<ImTextureID>(f.logo.id), ImVec2(w, h));
    centered_text("Kirby & The Amazing Mirror + Online", kSoftInk);
}

// ---- screens ----------------------------------------------------------------------------
void draw_update_bar(Front& f);

void screen_setup(Front& f) {
    ImGui::Dummy(ImVec2(0, 20));
    draw_logo(f, 360);
    ImGui::Dummy(ImVec2(0, 16));
    heading(T("¡Bienvenido!", "Welcome!"));
    centered_text(T("Para jugar necesitas tu propia copia de Kirby & The Amazing Mirror (USA). "
                    "Elígela una sola vez y KAMO la recordará.",
                    "To play you need your own copy of Kirby & The Amazing Mirror (USA). "
                    "Choose it once and KAMO will remember it."));
    ImGui::Dummy(ImVec2(0, 16));
    if (big_button(T("Elegir mi ROM", "Choose my ROM"))) pick_rom(f);
    draw_message(f);
    ImGui::Dummy(ImVec2(0, 16));
    centered_text(T("KAMO no incluye el juego. Solo se acepta el archivo .gba de la versión USA.",
                    "KAMO does not include the game. Only the USA .gba file is accepted."), kSoftInk);
    draw_update_bar(f);
}

// ---- update bar ---------------------------------------------------------------------------
// A pixel-art bar along the bottom of the menu that follows the updater: checking, downloading,
// verifying, installing. Its mascot is an original pink puffball drawn here pixel by pixel, not
// a sprite from the game. Two frames: standing and mid-step.
constexpr int kPuffSize = 16;
const char* const kPuff[2][kPuffSize] = {
    {"................", "......kkkk......", "....kkppppkk....", "...klpppppppk...",
     "..klpwppppwppk..", ".kplpbppppbpppk.", ".kpppbppppbpppk.", "kppppppppppppppk",
     "kppccppppppccppk", "kppppppkkppppppk", "kppppppppppppppk", ".kppppppppppppk.",
     ".kppppppppppppk.", "..kkppppppppkk..", "..rrrrkkkkrrrr..", ".RRRRRR..RRRRRR."},
    {"......kkkk......", "....kkppppkk....", "...klpppppppk...", "..klpwppppwppk..",
     ".kplpbppppbpppk.", ".kpppbppppbpppk.", "kppppppppppppppk", "kppccppppppccppk",
     "kppppppkkppppppk", "kppppppppppppppk", ".kppppppppppppk.", ".kppppppppppppk.",
     "..kkppppppppkk..", "...rrrrkkkkrrr..", "..RRRRR...RRRR..", "................"},
};

ImU32 puff_color(char c) {
    switch (c) {
    case 'k': return IM_COL32(84, 32, 70, 255);     // outline
    case 'p': return IM_COL32(255, 160, 196, 255);  // body
    case 'l': return IM_COL32(255, 222, 236, 255);  // shine
    case 'c': return IM_COL32(244, 96, 150, 255);   // cheeks
    case 'w': return IM_COL32(255, 255, 255, 255);  // eye glint
    case 'b': return IM_COL32(40, 28, 96, 255);     // eyes
    case 'r': return IM_COL32(226, 40, 72, 255);    // feet
    case 'R': return IM_COL32(168, 22, 52, 255);    // feet, shade
    default: return 0;
    }
}

void draw_puff(ImDrawList* dl, ImVec2 at, float px, int frame, bool flip) {
    for (int y = 0; y < kPuffSize; ++y)
        for (int x = 0; x < kPuffSize; ++x) {
            const ImU32 c = puff_color(kPuff[frame][y][flip ? kPuffSize - 1 - x : x]);
            if (!c) continue;
            const ImVec2 a(std::floor(at.x + x * px), std::floor(at.y + y * px));
            dl->AddRectFilled(a, ImVec2(a.x + px, a.y + px), c);
        }
}

// A four-point pixel star.
void draw_star(ImDrawList* dl, ImVec2 c, float px, ImU32 color) {
    c = ImVec2(std::floor(c.x), std::floor(c.y));
    dl->AddRectFilled(ImVec2(c.x - px * 0.5f, c.y - px * 1.5f), ImVec2(c.x + px * 0.5f, c.y + px * 1.5f), color);
    dl->AddRectFilled(ImVec2(c.x - px * 1.5f, c.y - px * 0.5f), ImVec2(c.x + px * 1.5f, c.y + px * 0.5f), color);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                                 x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

void draw_update_bar(Front& f) {
    using S = Updater::State;
    if (!f.updater.started()) return;
    const Uint32 now = SDL_GetTicks();
    const S state = f.updater.state();
    const auto info = f.updater.info();
    const bool can_install = f.updater.installed() && !info.installer_url.empty();
    if (!f.bar_since) f.bar_since = now;

    // A newer release was found: an installed copy updates by itself.
    if (state == S::Available && can_install && !f.auto_update) {
        f.auto_update = true;
        f.updater.download();
    }
    // Up to date, or the check failed: say so for a moment, then the bar slides away.
    const bool settles = state == S::UpToDate || (state == S::Failed && !f.updater.download_failed());
    if (!settles) f.bar_hide_at = 0;
    else if (!f.bar_hide_at) f.bar_hide_at = now + (state == S::UpToDate ? 2200 : 4500);
    float slide = std::min(1.0f, float(now - f.bar_since) / 280.0f);
    if (f.bar_hide_at && now > f.bar_hide_at)
        slide = 1.0f - std::min(1.0f, float(now - f.bar_hide_at) / 280.0f);
    if (slide <= 0) return;
    slide = 1 - (1 - slide) * (1 - slide);

    // The installer runs a moment after it checks out, so the player sees it happen.
    if (state == S::Ready && !f.install_failed) {
        if (!f.ready_since) f.ready_since = now;
        else if (now - f.ready_since > 1400) {
            if (f.updater.launch_installer()) { f.quit = true; return; }
            f.install_failed = true;
        }
    }

    const ImU32 ink = ImGui::ColorConvertFloat4ToU32(kInk);
    const ImU32 bad = ImGui::ColorConvertFloat4ToU32(kBad);
    const ImU32 good = ImGui::ColorConvertFloat4ToU32(kGood);
    const ImU32 pink = ImGui::ColorConvertFloat4ToU32(kPink);
    const ImU32 pink_dark = ImGui::ColorConvertFloat4ToU32(kPinkActive);
    const ImU32 pale = IM_COL32(255, 247, 251, 255);
    const ImU32 gold = IM_COL32(255, 200, 40, 255);

    std::string text;
    ImU32 text_color = ink;
    const char* action = nullptr;  // a button on the right, when the player has something to do
    const std::string dots((now / 350) % 4, '.');
    switch (state) {
    case S::Checking:
        text = std::string(T("Validando actualización", "Checking for updates")) + dots;
        break;
    case S::UpToDate:
        text = std::string(T("¡KAMO está al día! ", "KAMO is up to date! ")) + "v" KAMO_VERSION;
        text_color = good;
        break;
    case S::Available:
        if (can_install) {
            text = std::string(T("¡Versión nueva encontrada! ", "New version found! ")) + info.version;
        } else {
            text = std::string(T("Nueva versión ", "New version ")) + info.version + T(" disponible", " available");
            action = T("Descargar", "Download");
        }
        break;
    case S::Downloading:
        text = std::string(T("Actualizando a la ", "Updating to ")) + info.version + "... " +
               std::to_string(f.updater.progress()) + "%";
        break;
    case S::Verifying:
        text = std::string(T("Comprobando la descarga", "Verifying the download")) + dots;
        break;
    case S::Ready:
        if (f.install_failed) {
            text = T("No se pudo abrir el instalador.", "The installer could not be started.");
            text_color = bad;
            action = T("Abrir página", "Open page");
        } else {
            text = std::string(T("¡Listo! Instalando ", "Done! Installing ")) + info.version +
                   T(", KAMO se abrirá solo", ", KAMO will reopen by itself");
            text_color = good;
        }
        break;
    case S::Failed:
        if (f.updater.download_failed()) {
            text = std::string(T("No se pudo actualizar: ", "Could not update: ")) + f.updater.error();
            action = T("Reintentar", "Try again");
        } else {
            text = T("No se pudo validar la actualización.", "Could not check for updates.");
        }
        text_color = bad;
        break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth(), bar_h = 92;
    const float x0 = wp.x, y0 = wp.y + ImGui::GetWindowHeight() - bar_h * slide;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + bar_h), IM_COL32(255, 214, 231, 255));
    // Checkered pixel border that scrolls along the top.
    const float cell = 4;
    const int shift = int(now / 140);
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i * cell < W; ++i) {
            const bool dark = ((i + row + shift) & 1) != 0;
            dl->AddRectFilled(ImVec2(x0 + i * cell, y0 + row * cell),
                              ImVec2(x0 + (i + 1) * cell, y0 + (row + 1) * cell), dark ? pink_dark : pink);
        }

    // Text with a one-pixel-art drop shadow.
    ImFont* font = g_big_font ? g_big_font : ImGui::GetFont();
    const float font_size = 24;
    dl->AddText(font, font_size, ImVec2(x0 + 26, y0 + 14), IM_COL32(255, 255, 255, 255), text.c_str());
    dl->AddText(font, font_size, ImVec2(x0 + 24, y0 + 12), text_color, text.c_str());

    // The track: a row of pixel blocks.
    const float tx = x0 + 24, tw = W - 48, ty = y0 + 70, th = 12;
    dl->AddRectFilled(ImVec2(tx - 3, ty - 3), ImVec2(tx + tw + 3, ty + th + 3), ink);
    dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + th), pale);
    const float bw = 10, gap = 3;
    const int blocks = std::max(1, int((tw - gap) / (bw + gap)));
    const float step = (tw - gap) / blocks;
    const float px = 2, puff = kPuffSize * px;
    const float track_end = std::max(0.0f, tw - puff);

    float sx = 0, hop = 0;   // mascot position along the track, height above it
    int frame = 0;
    bool flip = false, stars = false;
    int lit = 0;             // blocks filled from the left
    float scan = -100;       // block index the scanning light is on
    switch (state) {
    case S::Checking: {
        const float t = float(now % 3000) / 1500.0f;  // walks across and back
        flip = t >= 1;
        sx = (t < 1 ? t : 2 - t) * track_end;
        frame = (now / 150) % 2;
        scan = (sx + puff / 2) / step;
        break;
    }
    case S::Downloading: {
        lit = blocks * f.updater.progress() / 100;
        sx = std::clamp(lit * step - puff / 2, 0.0f, track_end);
        frame = (now / 90) % 2;   // running
        break;
    }
    case S::Verifying:
        lit = blocks;
        scan = float((now / 40) % unsigned(blocks + 8));
        sx = track_end;
        hop = std::fabs(std::sin(now / 160.0f)) * 5;
        break;
    case S::UpToDate:
    case S::Ready:
        lit = blocks;
        sx = track_end;
        hop = std::fabs(std::sin(now / 130.0f)) * (state == S::Ready ? 12.0f : 6.0f);
        frame = hop > 3 ? 1 : 0;
        stars = !f.install_failed;
        break;
    case S::Available:
        sx = 0;
        hop = std::fabs(std::sin(now / 200.0f)) * 4;
        break;
    case S::Failed:
        sx = 0;
        break;
    }
    for (int i = 0; i < blocks; ++i) {
        const float bx = tx + gap + i * step;
        ImU32 c = IM_COL32(246, 200, 220, 255);
        if (i < lit) c = state == S::UpToDate ? good : pink;
        const float glow = 1.0f - std::min(1.0f, std::fabs(i - scan) / 4.0f);
        if (glow > 0) c = mix(c, i < lit ? IM_COL32(255, 255, 255, 255) : pink_dark, glow);
        if (state == S::Failed) c = IM_COL32(236, 200, 212, 255);
        dl->AddRectFilled(ImVec2(std::floor(bx), ty + 2), ImVec2(std::floor(bx + step - gap), ty + th - 2), c);
    }
    const ImVec2 at(tx + sx, ty - 3 - puff - std::floor(hop));
    draw_puff(dl, at, px, frame, flip);
    if (stars) {
        const ImVec2 spots[3] = {{-10, 6}, {puff + 8, 2}, {puff / 2, -10}};
        for (int k = 0; k < 3; ++k)
            if (((now / 160) + unsigned(k)) % 3 != 0)
                draw_star(dl, ImVec2(at.x + spots[k].x, at.y + spots[k].y), 2, k == 1 ? gold : IM_COL32(255, 255, 255, 255));
    }

    if (action) {
        const float bwid = 180;
        ImGui::SetCursorScreenPos(ImVec2(x0 + W - 24 - bwid, y0 + 14));
        if (button(action, bwid)) {
            if (state == S::Failed) { f.bar_since = now; f.updater.retry(); }
            else if (state == S::Available && f.updater.installed() && !info.installer_url.empty()) f.updater.download();
            else f.updater.open_release_page();
        }
    }
}

// Also leaves a room whose create the server has not answered yet: the leave reaches the
// server after the create, so the room is closed instead of staying listed with no host.
void leave_room(Front& f) {
    if (!f.np || !f.np->leave) return;
    if (f.create_sent || (f.np->in_lobby && f.np->in_lobby(f.np->ctx))) f.np->leave(f.np->ctx);
    f.create_sent = false;
}

void screen_menu(Front& f) {
    ImGui::Dummy(ImVec2(0, 24));
    draw_logo(f, 400);
    ImGui::Dummy(ImVec2(0, 28));
    // While an update downloads and installs, the game waits: it would be replaced under it.
    const bool updating = f.updater.busy() && !f.install_failed;
    if (big_button(T("Jugar solo", "Play alone"), 340, !updating)) {
        if (!check_rom(f.rom).empty()) { f.screen = Screen::Setup; return; }
        f.start_game = true;
    }
    if (big_button(T("Jugar online", "Play online"), 340, !updating)) {
        if (!check_rom(f.rom).empty()) { f.screen = Screen::Setup; return; }
        f.message.clear();
        f.screen = Screen::Online;
    }
    if (big_button(T("Opciones", "Options"))) { f.message.clear(); f.screen = Screen::Options; }
    if (big_button(T("Salir", "Quit"))) f.quit = true;
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 40));
    ImGui::TextDisabled("KAMO %s", KAMO_VERSION);
    draw_update_bar(f);
}

bool g_lobby_opened = false;

bool ensure_lobby(Front& f) {
    if (f.np) return true;
    try {
        RecompLauncherCGameInfo gi{};
        gbarecomp_seam::configure_netplay(gi, *f.options, f.dir.string());
        f.np = recomp_netplay_host_callbacks();
        g_lobby_opened = f.np != nullptr;
        f.lobby_version = f.options->netplay->build_identity.substr(0, 31);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "KAMO: online unavailable: %s\n", e.what());
        note(f, T("El modo online no está disponible en esta copia.", "Online play is not available in this copy."), true);
        f.screen = Screen::Menu;
        return false;
    }
    return f.np != nullptr;
}

void lobby_error(Front& f) {
    if (!f.np->last_error) return;
    const char* e = f.np->last_error(f.np->ctx);
    if (!e || !e[0]) return;
    std::string text = T("El servidor dijo: ", "The server said: ");
    if (!std::strcmp(e, "bad_password") || !std::strcmp(e, "wrong_password"))
        text = T("Contraseña incorrecta.", "Wrong password.");
    else if (!std::strcmp(e, "lobby_full") || !std::strcmp(e, "full"))
        text = T("La sala está llena.", "The room is full.");
    else if (!std::strcmp(e, "ice_not_connected") || !std::strcmp(e, "host_relay_unproven")) {
        text = T("Un jugador no pudo conectarse con el anfitrión. Espera unos segundos y pulsa "
                 "Empezar partida otra vez.",
                 "A player could not connect to the host. Wait a few seconds and press Start match again.");
    }
    else text += e;
    note(f, text, true);
    if (f.np->clear_last_error) f.np->clear_last_error(f.np->ctx);
}

// The lobby server only seats players signed in with Discord (once per PC; the key is kept
// in netplay_secret next to KAMO.exe). Returns true when signed in.
bool sign_in_card(Front& f) {
    const auto* np = f.np;
    if (!np->account_state) return true;
    const int state = np->account_state(np->ctx);
    if (state == RECOMP_LAUNCHER_ACCOUNT_SIGNED_IN) return true;
    ImGui::Dummy(ImVec2(0, 30));
    centered_text(T("Para jugar online, inicia sesión con Discord. Solo hace falta la primera vez.",
                    "To play online, sign in with Discord. You only need to do this once."));
    ImGui::Dummy(ImVec2(0, 16));
    if (state == RECOMP_LAUNCHER_ACCOUNT_WAITING) {
        centered_text(T("Termina el inicio de sesión en tu navegador...",
                        "Finish signing in in your browser..."), kSoftInk);
    } else {
        if (big_button(T("Iniciar sesión con Discord", "Sign in with Discord"), 400)) {
            if (np->account_login_begin(np->ctx) != 0) {
                const char* e = np->account_error ? np->account_error(np->ctx) : "";
                note(f, std::string(T("No se pudo iniciar sesión. ", "Could not sign in. ")) + (e ? e : ""), true);
            } else {
                f.message.clear();
            }
        }
        if (state == RECOMP_LAUNCHER_ACCOUNT_FAILED && np->account_error) {
            const char* e = np->account_error(np->ctx);
            if (e && e[0]) { ImGui::Spacing(); centered_text(e, kBad); }
        }
    }
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (soft_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
    return false;
}

// Developer switch: KAMO_LAN_TEST=1 hosts and lists rooms on this network only (two seats, no
// lobby server, no sign-in), to try the room flow with two windows on one PC.
bool lan_test() {
    static const bool on = [] { const char* e = SDL_getenv("KAMO_LAN_TEST"); return e && e[0] == '1'; }();
    return on;
}

void screen_online(Front& f) {
    if (!ensure_lobby(f)) return;
    const auto* np = f.np;
    heading(T("Jugar online", "Play online"));
    if (lan_test()) {
        if (!f.name[0]) std::snprintf(f.name, sizeof(f.name), "LAN-%u", unsigned(SDL_GetTicks() % 1000));
        np->set_player_name(np->ctx, f.name);
    } else if (!sign_in_card(f)) {
        return;
    }
    if (np->account_handle) {
        const char* handle = np->account_handle(np->ctx);
        if (handle && handle[0]) std::snprintf(f.name, sizeof(f.name), "%s", handle);
    }
    const bool online = lan_test() || np->connected(np->ctx);
    if (!online && !(np->connecting && np->connecting(np->ctx)) && SDL_GetTicks() >= f.next_list) {
        np->connect(np->ctx);
        f.next_list = SDL_GetTicks() + 3000;
    }
    if (np->in_lobby(np->ctx)) { f.screen = Screen::Room; return; }
    if (online && SDL_GetTicks() >= f.next_list) {
        np->request_list(np->ctx);
        f.next_list = SDL_GetTicks() + 2000;
    }
    lobby_error(f);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s %s", T("Jugando como", "Playing as"), f.name);
    ImGui::SameLine();
    if (np->account_sign_out && soft_button(T("Cerrar sesión", "Sign out"))) {
        np->account_sign_out(np->ctx);
        return;
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 228);
    const bool named = f.name[0] != '\0';
    if (button(T("Crear una sala", "Create a room"), 200, named && online)) {
        std::snprintf(f.room_name, sizeof(f.room_name), "%s %s", T("Sala de", "Room of"), f.name);
        f.message.clear();
        f.screen = Screen::Create;
    }
    ImGui::Spacing();

    ImGui::BeginChild("rooms", ImVec2(0, ImGui::GetContentRegionAvail().y - 70));
    if (!online) {
        ImGui::Dummy(ImVec2(0, 60));
        centered_text(T("Conectando con el servidor...", "Connecting to the server..."), kSoftInk);
    } else {
        int shown = 0;
        if (ImGui::BeginTable("list", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn(T("Sala", "Room"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(T("Jugadores", "Players"), ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableHeadersRow();
            for (int i = 0; i < np->list_count(np->ctx); ++i) {
                RecompLauncherCNetplayLobby row{};
                if (!np->list_get(np->ctx, i, &row)) continue;
                if (f.options->netplay && row.game_name != f.options->netplay->program_id) continue;
                const bool same_version = f.lobby_version == row.game_version;
                ++shown;
                ImGui::PushID(i);
                ImGui::TableNextRow(0, 44);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s%s", row.name, row.has_password ? T("  (privada)", "  (private)") : "");
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d / %d", row.player_count, row.max_slots);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (row.latency_ms >= 0) ImGui::Text("%d ms", row.latency_ms);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                if (!same_version) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", T("otra versión", "other version"));
                } else if (button(T("Unirme", "Join"), 130, named && row.player_count < row.max_slots)) {
                    f.joining_id = row.lobby_id;
                    f.join_password[0] = '\0';
                    if (row.has_password) ImGui::OpenPopup("password");
                    else {
                        char bind[64];
                        std::snprintf(bind, sizeof(bind), "0.0.0.0:%d", free_udp_port(7778));
                        if (np->join(np->ctx, row.lobby_id, "", bind) != 0)
                            note(f, T("No se pudo entrar en la sala.", "Could not join the room."), true);
                        else f.room_title = row.name;
                    }
                }
                if (ImGui::BeginPopupModal("password", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted(T("Esta sala es privada. Escribe la contraseña:",
                                             "This room is private. Type its password:"));
                    ImGui::SetNextItemWidth(320);
                    ImGui::InputText("##pw", f.join_password, sizeof(f.join_password), ImGuiInputTextFlags_Password);
                    if (button(T("Entrar", "Join"), 150)) {
                        char bind[64];
                        std::snprintf(bind, sizeof(bind), "0.0.0.0:%d", free_udp_port(7778));
                        if (np->join(np->ctx, f.joining_id.c_str(), f.join_password, bind) != 0)
                            note(f, T("No se pudo entrar en la sala.", "Could not join the room."), true);
                        else f.room_title = row.name;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (soft_button(T("Cancelar", "Cancel"), 150)) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!shown) {
            ImGui::Dummy(ImVec2(0, 40));
            centered_text(T("No hay salas abiertas ahora mismo. ¡Crea una e invita a tus amigos!",
                            "No rooms are open right now. Create one and invite your friends!"), kSoftInk);
        }
    }
    ImGui::EndChild();
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (soft_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
}

void screen_create(Front& f) {
    const auto* np = f.np;
    heading(T("Crear una sala", "Create a room"));
    ImGui::BeginChild("form", ImVec2(560, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextUnformatted(T("Nombre de la sala", "Room name"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##room", f.room_name, sizeof(f.room_name));
    ImGui::Spacing();
    ImGui::TextUnformatted(T("Jugadores", "Players"));
    for (int n = 2; n <= 4; ++n) {
        if (n > 2) ImGui::SameLine();
        char label[8];
        std::snprintf(label, sizeof(label), "%d", n);
        ImGui::RadioButton(label, &f.room_players, n);
    }
    ImGui::Spacing();
    ImGui::Checkbox(T("Sala privada (con contraseña)", "Private room (with a password)"), &f.room_private);
    if (f.room_private) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pw", T("Contraseña", "Password"), f.password, sizeof(f.password));
    } else {
        ImGui::TextDisabled("%s", T("Cualquiera podrá verla en la lista y entrar.", "Anyone can see it in the list and join."));
    }
    ImGui::EndChild();
    ImGui::Spacing();
    const bool ok = f.room_name[0] && (!f.room_private || f.password[0]);
    if (big_button(T("Crear", "Create"), 340, ok)) {
        char endpoint[64];
        std::snprintf(endpoint, sizeof(endpoint), "%s:%d", lan_test() ? "127.0.0.1" : "0.0.0.0", free_udp_port(7777));
        RecompLauncherCSettings settings{};
        if (np->create(np->ctx, f.room_name, endpoint, f.room_private ? f.password : "", &settings,
                       lan_test() ? 1 : 0, lan_test() ? 2 : f.room_players) != 0) {
            note(f, T("No se pudo crear la sala. Inténtalo otra vez.", "Could not create the room. Try again."), true);
        } else {
            f.room_title = f.room_name;
            f.create_sent = true;
            f.message.clear();
            f.screen = Screen::Room;
        }
    }
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (soft_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Online; }
}

// ---- room: host crown and chat ----------------------------------------------------------
// A small pixel crown, drawn here (no game art), marks the room's host.
constexpr int kCrownW = 9, kCrownH = 7;
const char* const kCrown[kCrownH] = {
    "y...y...y",
    "yy.yyy.yy",
    "yyyyyyyyy",
    "yryybyyry",
    "yyyyyyyyy",
    "ooooooooo",
    ".........",
};

// Draws the crown at the cursor and moves past it, centred on a line of `line_h` pixels.
void crown(float px = 2, float line_h = 0) {
    if (line_h <= 0) line_h = ImGui::GetFrameHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = kCrownW * px, h = (kCrownH - 1) * px;
    const float top = std::floor(at.y + (line_h - h) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int y = 0; y < kCrownH; ++y)
        for (int x = 0; x < kCrownW; ++x) {
            ImU32 c = 0;
            switch (kCrown[y][x]) {
            case 'y': c = IM_COL32(255, 196, 40, 255); break;
            case 'o': c = IM_COL32(214, 132, 16, 255); break;
            case 'r': c = IM_COL32(226, 40, 72, 255); break;
            case 'b': c = IM_COL32(70, 150, 255, 255); break;
            default: continue;
            }
            const ImVec2 a(std::floor(at.x + x * px), top + y * px);
            dl->AddRectFilled(a, ImVec2(a.x + px, a.y + px), c);
        }
    // A glint that comes and goes on the middle point.
    if ((SDL_GetTicks() / 300) % 7 == 0)
        dl->AddRectFilled(ImVec2(std::floor(at.x + 4 * px), top - px), ImVec2(std::floor(at.x + 5 * px), top), IM_COL32(255, 255, 255, 255));
    ImGui::Dummy(ImVec2(w, line_h));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Anfitrión", "Host"));
}

// The room's chat: every seated player sees every line. Lines come back from the server (or the
// LAN host), so a sent line appears once the room has it.
void draw_chat(Front& f, const std::string& host_name, float height) {
    const auto* np = f.np;
    ImGui::BeginChild("chat", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(kPinkActive, "%s", T("Chat de la sala", "Room chat"));
    const float input_h = ImGui::GetFrameHeight();
    ImGui::BeginChild("lines", ImVec2(0, ImGui::GetContentRegionAvail().y - input_h - ImGui::GetStyle().ItemSpacing.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    const int n = np->chat_count(np->ctx);
    std::uint32_t newest = 0;
    int shown = 0;
    for (int i = 0; i < n; ++i) {
        RecompLauncherCNetplayChatMessage m{};
        if (!np->chat_get(np->ctx, i, &m)) continue;
        newest = m.seq;
        ++shown;
        ImGui::PushTextWrapPos(0);
        if (m.is_system || !m.from[0]) {
            ImGui::TextDisabled("%s", m.text);
        } else {
            const float line = ImGui::GetTextLineHeight();
            if (!host_name.empty() && host_name == m.from) { crown(2, line); ImGui::SameLine(0, 4); }
            ImGui::TextColored(m.is_local ? kPinkActive : kInk, "%s:", m.from);
            ImGui::SameLine(0, 6);
            ImGui::TextUnformatted(m.text);
        }
        ImGui::PopTextWrapPos();
    }
    if (!shown) ImGui::TextDisabled("%s", T("¡Saluda a tu equipo!", "Say hi to your team!"));
    if (newest != f.chat_seen) { ImGui::SetScrollHereY(1.0f); f.chat_seen = newest; }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    const float send_w = 90;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - send_w - ImGui::GetStyle().ItemSpacing.x);
    bool send = ImGui::InputTextWithHint("##chat", T("Escribe un mensaje...", "Type a message..."), f.chat_line,
                                         sizeof(f.chat_line), ImGuiInputTextFlags_EnterReturnsTrue);
    if (send) ImGui::SetKeyboardFocusHere(-1);  // keep typing after Enter
    ImGui::SameLine();
    send |= button(T("Enviar", "Send"), send_w, f.chat_line[0] != 0);
    if (send && f.chat_line[0]) {
        if (np->chat_send(np->ctx, f.chat_line) == 0) f.chat_line[0] = '\0';
        else note(f, T("No se pudo enviar el mensaje.", "The message could not be sent."), true);
    }
    ImGui::EndChild();
}

void screen_room(Front& f) {
    const auto* np = f.np;
    lobby_error(f);
    if (!np->in_lobby(np->ctx)) {
        // Joining takes a moment; a room that closed sends the player back to the list.
        if (!f.waiting_since) f.waiting_since = SDL_GetTicks();
        if (SDL_GetTicks() - f.waiting_since > 8000) {
            f.waiting_since = 0;
            leave_room(f);
            note(f, T("Saliste de la sala.", "You left the room."), true);
            f.screen = Screen::Online;
            return;
        }
        heading(f.room_title.c_str());
        centered_text(T("Entrando...", "Joining..."), kSoftInk);
        return;
    }
    f.create_sent = false;
    f.waiting_since = 0;
    if (np->launch_pending(np->ctx)) {
        RecompLauncherCNetplayLaunch launch{};
        if (np->fill_launch(np->ctx, &launch) && launch.enabled) {
            np->clear_launch_pending(np->ctx);
            try {
                gbarecomp_seam::accept_netplay(launch, *f.options);
                if (f.continue_saved) {
                    const auto saved = newest_online_save(f.dir);
                    if (!saved.empty()) f.options->netplay->resume_path = saved;
                }
                f.start_game = true;
                return;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "KAMO: cannot start the match: %s\n", e.what());
                note(f, T("No se pudo empezar la partida.", "The match could not start."), true);
            }
        }
    }

    const bool host = np->is_host(np->ctx) != 0;
    const int count = np->member_count(np->ctx);
    const int max = np->lobby_max_slots ? np->lobby_max_slots(np->ctx) : 4;
    heading(f.room_title.c_str());
    char sub[96];
    std::snprintf(sub, sizeof(sub), T("%d de %d jugadores", "%d of %d players"), count, max);
    centered_text(sub, kSoftInk);
    ImGui::Spacing();

    int slowest = -1;
    std::string host_name;
    const bool has_chat = np->chat_send && np->chat_count && np->chat_get;
    const float panel_h = 250;
    const float members_w = has_chat ? std::floor(ImGui::GetContentRegionAvail().x * 0.56f) : 0;
    ImGui::BeginChild("members", ImVec2(members_w, panel_h), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTable("m", 4, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, has_chat ? 96 : 110);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, has_chat ? 96 : 120);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, has_chat ? 64 : 90);
        for (int i = 0; i < count; ++i) {
            RecompLauncherCNetplayMember m{};
            if (!np->member_get(np->ctx, i, &m)) continue;
            if (!m.is_local && m.latency_ms > slowest) slowest = m.latency_ms;
            ImGui::TableNextRow(0, 40);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kPinkActive, "%s %d", T("Jugador", "Player"), i + 1);
            ImGui::TableNextColumn();
            if (m.is_host) {
                host_name = m.display_name;
                crown();
                ImGui::SameLine(0, 6);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s", m.display_name, m.is_local ? T("  - tú", "  - you") : "");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (m.ready) ImGui::TextColored(kGood, "%s", T("Listo", "Ready"));
            else ImGui::TextDisabled("%s", T("Esperando", "Waiting"));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (!m.is_local && m.latency_ms >= 0) ImGui::Text("%d ms", m.latency_ms);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (has_chat) {
        ImGui::SameLine();
        draw_chat(f, host_name, panel_h);
    }

    if (!newest_online_save(f.dir).empty()) {
        ImGui::Checkbox(T("Continuar la última partida guardada", "Continue the last saved match"), &f.continue_saved);
        if (f.continue_saved)
            ImGui::TextDisabled("%s", T("Todos los jugadores deben marcar esta opción.", "Every player must tick this."));
    }
    ImGui::Spacing();

    const bool ready = np->local_ready(np->ctx) != 0;
    const float row = 260 * 2 + 12;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(row));
    if (button(ready ? T("Ya no estoy listo", "Not ready") : T("Estoy listo", "I'm ready"), 260))
        np->set_ready(np->ctx, ready ? 0 : 1);
    ImGui::SameLine();
    if (host) {
        const bool can_start = count >= 2 && np->all_ready(np->ctx);
        if (button(T("Empezar partida", "Start match"), 260, can_start)) {
            const int delay = f.settings.input_delay > 0 ? f.settings.input_delay : delay_from_ping(slowest);
            if (np->input_delay_set) np->input_delay_set(np->ctx, delay);
            RecompLauncherCSettings settings{};
            if (np->request_start(np->ctx, &settings) != 0)
                note(f, T("No se pudo empezar. ¿Están todos listos?", "Could not start. Is everyone ready?"), true);
        }
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", T("El anfitrión empieza la partida.", "The host starts the match."));
    }
    if (host && !(count >= 2 && np->all_ready(np->ctx)))
        centered_text(T("La partida empieza cuando todos (2 o más) estén listos.",
                        "The match can start once everyone (2 or more) is ready."), kSoftInk);
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (soft_button(T("Salir de la sala", "Leave room"), 200)) {
        leave_room(f);
        f.message.clear();
        f.screen = Screen::Online;
    }
}

void screen_options(Front& f) {
    heading(T("Opciones", "Options"));
    bool changed = false;
    ImGui::BeginChild("opts", ImVec2(0, ImGui::GetContentRegionAvail().y - 70), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem(T("Pantalla y sonido", "Display and sound"))) {
            ImGui::Spacing();
            ImGui::SetNextItemWidth(320);
            changed |= ImGui::SliderInt(T("Tamaño de la ventana", "Window size"), &f.settings.scale, 1, 6, "x%d");
            changed |= ImGui::Checkbox(T("Pantalla completa", "Full screen"), &f.settings.fullscreen);
            ImGui::SetNextItemWidth(320);
            changed |= ImGui::SliderInt(T("Volumen", "Volume"), &f.settings.volume, 0, 100, "%d%%");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Controles", "Controls"))) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Teclado. Los mandos funcionan solos.", "Keyboard. Gamepads work on their own."));
            if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 120);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                for (int i = 0; i < kButtonCount; ++i) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(f.keys[i].label);
                    ImGui::TableNextColumn();
                    const std::string label = f.capture_key == i ? T("Pulsa una tecla...", "Press a key...") : f.keys[i].key;
                    if (soft_button(label.c_str(), 240)) f.capture_key = i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Atajos", "Shortcuts"))) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", T("Teclas mientras juegas.", "Keys while you play."));
            struct Shortcut { const char* keys; const char* es; const char* en; };
            static const Shortcut shortcuts[] = {
                {"Esc", "Menú del juego (y volver al menú principal)", "Game menu (and back to the main menu)"},
                {"Start + Select", "Menú del juego con mando", "Game menu on a gamepad"},
                {"Alt + Enter", "Pantalla completa", "Full screen"},
                {"Shift + F1..F9", "Guardado rápido en la ranura 1-9 (solo)", "Quick save to slot 1-9 (alone)"},
                {"F1..F9", "Cargar la ranura 1-9 (solo)", "Load slot 1-9 (alone)"},
                {"Shift + P", "Pausa (solo)", "Pause (alone)"},
                {"Tab", "Acelerar mientras la mantienes (solo)", "Fast forward while held (alone)"},
                {"F", "Mostrar FPS", "Show FPS"},
            };
            if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& sc : shortcuts) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kPinkActive, "%s", sc.keys);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(T(sc.es, sc.en));
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::TextWrapped("%s", T("Online: Esc o cerrar la ventana guarda la partida y vuelve al menú; se continúa desde la sala.",
                                       "Online: Esc or closing the window saves the match and returns to the menu; continue it from the room."));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Online")) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", T("Retraso de entrada: cuántos fotogramas esperan los botones para que todos "
                                       "vean lo mismo. Automático lo elige según el ping. Menos = responde antes; "
                                       "si el juego da tirones, súbelo. Lo decide el anfitrión.",
                                       "Input delay: how many frames button presses wait so everyone sees the same "
                                       "game. Automatic picks it from the ping. Lower responds sooner; raise it if "
                                       "the game stutters. The host decides."));
            const char* items[] = {"Auto", "2", "3", "4", "5", "6", "7", "8"};
            int index = f.settings.input_delay ? f.settings.input_delay - 1 : 0;
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo(T("Retraso", "Delay"), &index, items, 8)) {
                f.settings.input_delay = index ? index + 1 : 0;
                changed = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Juego", "Game"))) {
            ImGui::Spacing();
            ImGui::TextUnformatted(T("Tu ROM:", "Your ROM:"));
            ImGui::TextDisabled("%s", utf8(f.rom).c_str());
            if (button(T("Cambiar ROM", "Change ROM"), 220)) pick_rom(f);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    if (changed) save_settings(f.dir, f.settings);
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (soft_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
}

}  // namespace

bool run_frontend(std::vector<std::string>& args, gbarecomp::RunOptions& options,
                  const std::filesystem::path& dir) {
    detect_language();
    Front f;
    f.dir = dir;
    f.options = &options;
    f.settings = load_settings(dir);
    load_keys(dir, f.keys);
    std::snprintf(f.name, sizeof(f.name), "%s", f.settings.name.c_str());
    f.rom = load_rom_path(dir);
    f.screen = check_rom(f.rom).empty() ? Screen::Menu : Screen::Setup;
    // Under RecompLauncher the launcher installs updates; KAMO does not update itself.
    const char* launcher = SDL_getenv("RECOMPLAUNCHER");
    if (!(launcher && launcher[0] == '1')) f.updater.start(std::filesystem::exists(dir / "unins000.exe"));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "KAMO: cannot open the menu: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("KAMO", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 640,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl) {
        std::fprintf(stderr, "KAMO: cannot open the menu: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER);
        return false;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowMinimumSize(window, 820, 600);
    set_window_icon(window, dir / "assets" / "kamo" / "icon.png");

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    const auto font = (dir / "assets" / "fonts" / "LatoLatin-Bold.ttf").string();
    const auto regular = (dir / "assets" / "fonts" / "LatoLatin-Regular.ttf").string();
    if (std::filesystem::exists(font) && std::filesystem::exists(regular)) {
        io.Fonts->AddFontFromFileTTF(regular.c_str(), 21.0f);
        g_big_font = io.Fonts->AddFontFromFileTTF(font.c_str(), 26.0f);
        g_title_font = io.Fonts->AddFontFromFileTTF(font.c_str(), 38.0f);
    } else {
        io.Fonts->AddFontDefault();
        g_big_font = g_title_font = io.Fonts->Fonts[0];
    }
    apply_style();
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 130");
    f.logo = load_png(dir / "assets" / "kamo" / "logo.png");

    while (!f.start_game && !f.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (f.capture_key >= 0 && e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym != SDLK_ESCAPE) {
                    f.keys[f.capture_key].key = SDL_GetKeyName(e.key.keysym.sym);
                    save_keys(dir, f.keys);
                }
                f.capture_key = -1;
                continue;
            }
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) f.quit = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) f.quit = true;
        }
        if (f.np && f.np->pump) f.np->pump(f.np->ctx);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("KAMO", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (f.screen) {
        case Screen::Setup: screen_setup(f); break;
        case Screen::Menu: screen_menu(f); break;
        case Screen::Online: screen_online(f); break;
        case Screen::Create: screen_create(f); break;
        case Screen::Room: screen_room(f); break;
        case Screen::Options: screen_options(f); break;
        }
        ImGui::End();
        ImGui::Render();
        glViewport(0, 0, int(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   int(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(kBackground.x, kBackground.y, kBackground.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    if (!f.start_game) leave_room(f);

    if (f.logo.id) glDeleteTextures(1, &f.logo.id);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context);
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER);
    if (!f.start_game) return false;

    args.push_back("--rom");
    args.push_back(utf8(f.rom));
    args.push_back("--scale");
    args.push_back(std::to_string(f.settings.scale));
    args.push_back("--volume");
    args.push_back(std::to_string(f.settings.volume));
    if (f.settings.fullscreen) args.push_back("--fullscreen=1");
    return true;
}

void close_online() {
    if (!g_lobby_opened) return;
    g_lobby_opened = false;
    recomp_netplay_host_disconnect();
}

}  // namespace kamo
