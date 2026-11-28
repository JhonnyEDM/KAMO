// KAMO's menu window: first-run ROM setup, Play alone, Play online (rooms on the public lobby
// server through recomp-ui's lobby backend), Options and automatic updates. Drawn with the
// Dear ImGui copy that recomp-ui already builds into KAMO.

#include "kamo_frontend.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <map>
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
#include "recomp_net/host_relay.h"
#include "recomp_net/lobby_client.h"

#include "kamo_game.h"
#include "kamo_kirby.h"
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
ImVec4 kBackground = rgb(255, 232, 241);
ImVec4 kInk = rgb(84, 32, 70);
ImVec4 kSoftInk = rgb(150, 92, 128);
ImVec4 kPink = rgb(236, 72, 132);
ImVec4 kPinkHover = rgb(244, 104, 156);
ImVec4 kPinkActive = rgb(206, 46, 108);
ImVec4 kCard = rgb(255, 247, 251);
ImVec4 kGood = rgb(46, 160, 98);
ImVec4 kBad = rgb(214, 54, 72);
ImVec4 kSoft = rgb(255, 214, 231), kSoftHover = rgb(255, 196, 220), kSoftActive = rgb(255, 178, 208);

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

Texture load_png(const std::filesystem::path& path, bool pixel_art = false) {
    Texture t;
    int n = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &t.w, &t.h, &n, 4);
    if (!pixels) return {};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    const GLint filter = pixel_art ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
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

// The night palette of the pixel-art main menu, used on every screen when that art is present:
// cream text on deep blue and plum, pink actions, gold edges.
GLuint g_plate = 0;  // the menu's button plate texture; 0 keeps the plain buttons

void apply_night_style() {
    kBackground = rgb(23, 50, 93);
    kInk = rgb(255, 249, 223);
    kSoftInk = rgb(190, 176, 236);
    kPink = rgb(240, 60, 137);
    kPinkHover = rgb(255, 96, 164);
    kPinkActive = rgb(255, 128, 186);
    kCard = rgb(36, 25, 66, 0.93f);
    kGood = rgb(123, 227, 160);
    kBad = rgb(255, 122, 138);
    kSoft = rgb(58, 42, 102); kSoftHover = rgb(76, 56, 132); kSoftActive = rgb(46, 33, 80);
    apply_style();
    ImGuiStyle& s = ImGui::GetStyle();
    s.FrameRounding = 6; s.ChildRounding = 6; s.PopupRounding = 6; s.TabRounding = 4;
    s.GrabRounding = 4; s.ScrollbarRounding = 4; s.FrameBorderSize = 1; s.PopupBorderSize = 2;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Border] = rgb(255, 227, 109, 0.55f);
    c[ImGuiCol_FrameBg] = rgb(46, 33, 80);
    c[ImGuiCol_FrameBgHovered] = rgb(58, 42, 102);
    c[ImGuiCol_FrameBgActive] = rgb(69, 48, 122);
    c[ImGuiCol_Header] = rgb(58, 42, 102);
    c[ImGuiCol_HeaderHovered] = rgb(76, 56, 132);
    c[ImGuiCol_HeaderActive] = rgb(240, 60, 137);
    c[ImGuiCol_TableHeaderBg] = rgb(58, 42, 102);
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.05f);
    c[ImGuiCol_TableBorderLight] = rgb(90, 70, 144);
    c[ImGuiCol_TableBorderStrong] = rgb(90, 70, 144);
    c[ImGuiCol_Separator] = rgb(90, 70, 144);
    c[ImGuiCol_CheckMark] = rgb(255, 227, 109);
    c[ImGuiCol_SliderGrab] = rgb(240, 60, 137);
    c[ImGuiCol_SliderGrabActive] = rgb(255, 96, 164);
    c[ImGuiCol_Tab] = rgb(46, 33, 80);
    c[ImGuiCol_TabHovered] = rgb(255, 96, 164);
    c[ImGuiCol_TabSelected] = rgb(240, 60, 137);
    c[ImGuiCol_ScrollbarGrab] = rgb(106, 85, 168);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(130, 108, 196);
    c[ImGuiCol_PopupBg] = rgb(36, 25, 66);
    c[ImGuiCol_ModalWindowDimBg] = rgb(10, 6, 24, 0.6f);
    c[ImGuiCol_NavCursor] = rgb(255, 227, 109);
    c[ImGuiCol_TextSelectedBg] = rgb(240, 60, 137, 0.45f);
}

ImU32 hex(unsigned rgb, int a = 255) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a); }

// The pink plate button of the menu art: hover glow, press, keyboard focus arrow.
bool plate_button(const char* id, const char* label, ImVec2 pos, ImVec2 size, float font_size, bool enabled);

// ---- small widgets ----------------------------------------------------------------------
ImFont* g_title_font = nullptr;
ImFont* g_big_font = nullptr;

float center_x(float width) { return std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f); }

bool big_button(const char* label, float width = 340, bool enabled = true) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(width));
    if (g_plate) {
        const ImVec2 size(width, std::floor(width * 131 / 586));
        ImGui::PushID(label);
        const bool pressed = plate_button("##big", label, ImGui::GetCursorScreenPos(), size, size.y * 0.36f, enabled);
        ImGui::PopID();
        return pressed;
    }
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
    ImGui::PushStyleColor(ImGuiCol_Button, kSoft);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSoftHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSoftActive);
    const bool pressed = ImGui::Button(label, ImVec2(width, 0));
    ImGui::PopStyleColor(3);
    return pressed;
}

// "Back" and "Leave room" at the bottom left: a small plate with the menu art.
bool back_button(const char* label, float width) {
    if (!g_plate) return soft_button(label, width);
    // One height for every bottom button; wide enough for its label.
    (void)width;
    constexpr float h = 41, font = 18;
    ImFont* f = g_big_font ? g_big_font : ImGui::GetFont();
    const float text_w = f->CalcTextSizeA(font, FLT_MAX, 0, label).x;
    const ImVec2 size(std::floor(std::max(h * 586 / 131, text_w + 70)), h);
    ImGui::PushID(label);
    const bool pressed = plate_button("##back", label, ImGui::GetCursorScreenPos(), size, font, true);
    ImGui::PopID();
    return pressed;
}

// A gold pixel edge with a plum drop shadow around the panel (child window) just ended.
void panel_border() {
    if (!g_plate) return;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(a.x - 1, a.y + 3), ImVec2(b.x + 5, b.y + 6), hex(0x150c26, 200), 8);
    dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), hex(0xffe36d), 8, 0, 2);
    dl->AddRect(ImVec2(a.x - 5, a.y - 5), ImVec2(b.x + 5, b.y + 5), hex(0x50266d), 9, 0, 2);
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
    if (g_plate) {
        const float width = std::min(620.0f, ImGui::CalcTextSize(text).x);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float x = std::floor(p.x + center_x(width));
        ImGui::GetWindowDrawList()->AddText(ImVec2(x + 3, p.y + 3), hex(0x50266d), text);
    }
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

// The input delay a match starts with (frames), from the host's round trips to its guests.
// It must cover the slowest path an input takes: two seats talk directly (half the round trip);
// with three or four the host relays, so a guest's input reaches another guest through the
// host (half of each round trip, the two slowest, plus a frame for that extra hop). The lobby
// measures through its server, which overstates a direct link: the start errs on the safe
// side and the match's own controller then lowers it within seconds (it raises it as well
// when inputs arrive late). The floor 3 is that controller's own floor.
struct StartDelay { int frames; int path_ms; };
StartDelay start_delay(std::vector<int> rtts) {
    constexpr double kFrameMs = 1000.0 / 59.7275;
    std::erase_if(rtts, [](int r) { return r < 0; });
    if (rtts.empty()) return {5, -1};
    std::sort(rtts.rbegin(), rtts.rend());
    const bool hub = rtts.size() >= 2;
    const int path = hub ? (rtts[0] + rtts[1]) / 2 : rtts[0] / 2;
    const int frames = int(std::ceil(path / kFrameMs)) + (hub ? 1 : 0);
    return {std::clamp(frames, 3, 10), path};
}

// The match's own delay controller (recomp-net) keeps adjusting the delay while playing. A
// delay fixed in Options turns it off for that match, so the number chosen is the one used.
void set_engine_auto_delay(bool on) {
#if defined(_WIN32)
    _putenv_s("RBE_RB_AUTO_DELAY", on ? "1" : "0");
#else
    setenv("RBE_RB_AUTO_DELAY", on ? "1" : "0", 1);
#endif
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
    // The main menu's pixel art (assets/kamo/menu): title art, lower scene, the side strips that
    // fill a window wider than the art, and the button plate.
    struct MenuArt { Texture hero, scene, wing_left, wing_right, button; } art;
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
    std::string relay_seen;     // last connection status written to the online log
    int logged_count = -1;      // last player count written to the online log
    bool logged_in_room = false;
    char chat_line[200] = {};
    std::map<std::string, std::uint32_t> kirby_colors;  // the room's colours, by display name
    int color_sent_count = -1;                          // player count when ours was last sent
    std::uint32_t color_sent = 0;
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
void screen_menu_plain(Front& f);

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
    const bool can_install = f.updater.can_self_update();
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

    // Fixed colours: the bar is light pink on every theme.
    const ImU32 ink = IM_COL32(84, 32, 70, 255);
    const ImU32 bad = IM_COL32(214, 54, 72, 255);
    const ImU32 good = IM_COL32(46, 160, 98, 255);
    const ImU32 pink = IM_COL32(236, 72, 132, 255);
    const ImU32 pink_dark = IM_COL32(206, 46, 108, 255);
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
            text = T("No se pudo instalar la actualización.", "The update could not be installed.");
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
            else if (state == S::Available && can_install) f.updater.download();
            else f.updater.open_release_page();
        }
    }
}

// ---- online log -------------------------------------------------------------------------
// netplay/online.log next to KAMO.exe records what a room and its match launch did (room,
// players, connection status, the route the match took, every error code), so a failed online
// start can be explained afterwards. Each line also goes to stderr. Over 512 KB the log moves
// to online.old.log. Public addresses are shortened to their last number.
std::filesystem::path g_online_log;

std::string short_endpoint(const std::string& endpoint) {
    const auto colon = endpoint.rfind(':');
    const std::string host = endpoint.substr(0, colon);
    if (host.empty() || host == "0.0.0.0" || host == "127.0.0.1" || host.rfind("192.168.", 0) == 0 ||
        host.rfind("10.", 0) == 0)
        return endpoint.empty() ? "-" : endpoint;
    const auto dot = host.rfind('.');
    return "x.x.x" + (dot == std::string::npos ? std::string(".?") : host.substr(dot)) +
           (colon == std::string::npos ? "" : endpoint.substr(colon));
}

void online_log(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "KAMO online: %s\n", line);
    if (g_online_log.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(g_online_log.parent_path(), ec);
    const auto size = std::filesystem::file_size(g_online_log, ec);
    if (!ec && size > 512 * 1024) {
        auto old = g_online_log;
        old.replace_filename("online.old.log");
        std::filesystem::rename(g_online_log, old, ec);
    }
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    std::ofstream(g_online_log, std::ios::app) << stamp << "  " << line << "\n";
}

const char* transport_name(gbarecomp::GbaNetplayTransport t) {
    switch (t) {
    case gbarecomp::GbaNetplayTransport::Relay: return "server relay";
    case gbarecomp::GbaNetplayTransport::Pair: return "direct pair";
    case gbarecomp::GbaNetplayTransport::Hub: return "host hub (this PC carries the match)";
    case gbarecomp::GbaNetplayTransport::HubGuest: return "guest of the host hub";
    }
    return "?";
}

// Also leaves a room whose create the server has not answered yet: the leave reaches the
// server after the create, so the room is closed instead of staying listed with no host.
void leave_room(Front& f) {
    if (!f.np || !f.np->leave) return;
    if (f.create_sent || (f.np->in_lobby && f.np->in_lobby(f.np->ctx))) {
        online_log("left the room");
        f.np->leave(f.np->ctx);
    }
    f.create_sent = false;
    f.logged_in_room = false;
    f.logged_count = -1;
    f.kirby_colors.clear();
    f.color_sent_count = -1;
    f.relay_seen.clear();
}

// ---- main menu --------------------------------------------------------------------------
// The pixel-art title screen. Layout follows the design (a 1182x1330 stage): the stage keeps
// its aspect, centred, at most as wide as the window; the side strips stretch over what is left.
// Over a sky gradient: the title art on top, the lower scene from 46% down, and four buttons
// (plate image + text) in a column at 25.4% / 47% of the stage, 49.6% wide.
constexpr float kStageW = 1182, kStageH = 1330;

void sky_gradient(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    // #17325d until 45%, #403169 at 78%, #ff681e at the bottom.
    const float h = b.y - a.y;
    const float y45 = a.y + h * 0.45f, y78 = a.y + h * 0.78f;
    dl->AddRectFilled(a, ImVec2(b.x, y45), hex(0x17325d));
    dl->AddRectFilledMultiColor(ImVec2(a.x, y45), ImVec2(b.x, y78), hex(0x17325d), hex(0x17325d), hex(0x403169), hex(0x403169));
    dl->AddRectFilledMultiColor(ImVec2(a.x, y78), b, hex(0x403169), hex(0x403169), hex(0xff681e), hex(0xff681e));
}

// One menu button: the plate image with the label, hover glow, press and keyboard focus.
bool plate_button(const char* id, const char* label, ImVec2 pos, ImVec2 size, float font_size, bool enabled) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, size) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const bool focused = enabled && ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // hover/focus: a little to the right and 2.5% bigger; held: 2 px down and 2% smaller.
    float scale = 1, dx = 0, dy = 0;
    if (held) { scale = 0.98f; dy = 2; }
    else if (hovered || focused) { scale = 1.025f; dx = 2; }
    const ImVec2 c(pos.x + size.x / 2 + dx, pos.y + size.y / 2 + dy);
    const ImVec2 a(std::floor(c.x - size.x * scale / 2), std::floor(c.y - size.y * scale / 2));
    const ImVec2 b(std::floor(c.x + size.x * scale / 2), std::floor(c.y + size.y * scale / 2));
    const float round = (b.y - a.y) * 0.32f;

    if ((hovered || focused) && !held)  // warm glow behind the plate
        for (int i = 3; i >= 1; --i)
            dl->AddRectFilled(ImVec2(a.x - i * 2.0f, a.y - i * 2.0f), ImVec2(b.x + i * 2.0f, b.y + i * 2.0f),
                              hex(0xffd94d, 26), round + i * 2.0f);
    ImU32 tint = IM_COL32_WHITE;
    if (!enabled) tint = IM_COL32(150, 140, 160, 210);
    else if (held) tint = IM_COL32(230, 230, 230, 255);
    dl->AddImage(static_cast<ImTextureID>(g_plate), a, b, ImVec2(0, 0), ImVec2(1, 1), tint);
    if ((hovered || focused) && !held) {  // brighter plate
        const float in = (b.y - a.y) * 0.14f;
        dl->AddRectFilled(ImVec2(a.x + in, a.y + in), ImVec2(b.x - in, b.y - in), IM_COL32(255, 255, 255, 34), round * 0.7f);
    }

    // The label: white with a plum shadow, centred a little above the middle (the plate's lip).
    ImFont* font = g_big_font ? g_big_font : ImGui::GetFont();
    const float fs = font_size * scale;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    const ImVec2 at(std::floor(c.x - ts.x / 2), std::floor(c.y - ts.y / 2 - (b.y - a.y) * 0.04f));
    dl->AddText(font, fs, ImVec2(at.x + fs * 0.09f, at.y + fs * 0.13f), hex(0x98266a), label);
    dl->AddText(font, fs, at, enabled ? IM_COL32_WHITE : IM_COL32(235, 225, 240, 255), label);

    if (focused) {  // keyboard / gamepad focus: a yellow arrow to the left
        const float s = fs * 0.55f, x = a.x - size.x * 0.07f, y = c.y;
        dl->AddTriangleFilled(ImVec2(x + 2, y - s / 2 + 2), ImVec2(x + 2, y + s / 2 + 2), ImVec2(x + s + 2, y + 2), hex(0x50266d));
        dl->AddTriangleFilled(ImVec2(x, y - s / 2), ImVec2(x, y + s / 2), ImVec2(x + s, y), hex(0xfff29b));
    }
    return pressed;
}

bool menu_art_ready(const Front& f) {
    return f.art.hero.id && f.art.scene.id && f.art.button.id;
}

void screen_menu(Front& f) {
    if (!menu_art_ready(f)) { screen_menu_plain(f); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth(), H = ImGui::GetWindowHeight();
    sky_gradient(dl, wp, ImVec2(wp.x + W, wp.y + H));

    const float sw = std::floor(std::min(W, H * kStageW / kStageH)), sh = std::floor(sw * kStageH / kStageW);
    const float sx = std::floor(wp.x + (W - sw) / 2), sy = std::floor(wp.y + (H - sh) / 2);
    if (sx > wp.x + 1) {  // side strips, stretched like the design's wings
        if (f.art.wing_left.id)
            dl->AddImage(static_cast<ImTextureID>(f.art.wing_left.id), ImVec2(wp.x, wp.y), ImVec2(sx, wp.y + H));
        if (f.art.wing_right.id)
            dl->AddImage(static_cast<ImTextureID>(f.art.wing_right.id), ImVec2(sx + sw, wp.y), ImVec2(wp.x + W, wp.y + H));
    }
    const float scene_top = std::floor(sy + sh * 0.46015f);
    dl->AddImage(static_cast<ImTextureID>(f.art.scene.id), ImVec2(sx, scene_top), ImVec2(sx + sw, sy + sh));
    const float hero_h = std::floor(sw * f.art.hero.h / std::max(1, f.art.hero.w));
    dl->AddImage(static_cast<ImTextureID>(f.art.hero.id), ImVec2(sx, sy), ImVec2(sx + sw, sy + hero_h));

    // While an update downloads and installs, the game waits: it would be replaced under it.
    const bool updating = f.updater.busy() && !f.install_failed;
    const ImVec2 bsize(std::floor(sw * 0.4958f), std::floor(sw * 0.4958f * 131 / 586));
    const float gap = std::max(1.0f, std::floor(sw * 0.0024f));
    const float font_size = std::max(16.0f, sw * 0.05f);
    float by = std::floor(sy + sh * 0.4699f);
    const float bx = std::floor(sx + sw * 0.2538f);
    auto next = [&]() { const ImVec2 at(bx, by); by += bsize.y + gap; return at; };

    if (plate_button("##solo", T("Jugar solo", "Play alone"), next(), bsize, font_size, !updating)) {
        if (!check_rom(f.rom).empty()) { f.screen = Screen::Setup; return; }
        f.start_game = true;
    }
    if (plate_button("##online", T("Jugar online", "Play online"), next(), bsize, font_size, !updating)) {
        if (!check_rom(f.rom).empty()) { f.screen = Screen::Setup; return; }
        f.message.clear();
        f.screen = Screen::Online;
    }
    if (plate_button("##options", T("Opciones", "Options"), next(), bsize, font_size, true)) {
        f.message.clear();
        f.screen = Screen::Options;
    }
    if (plate_button("##quit", T("Salir", "Quit"), next(), bsize, font_size, true)) f.quit = true;

    if (!f.message.empty()) {  // a note (ROM verified, ...) on a dark plate under the buttons
        ImFont* font = ImGui::GetFont();
        const float fs = std::max(14.0f, sw * 0.03f);
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, sw * 0.8f, f.message.c_str());
        const ImVec2 at(std::floor(sx + (sw - ts.x) / 2), std::floor(by + gap * 4));
        dl->AddRectFilled(ImVec2(at.x - 12, at.y - 6), ImVec2(at.x + ts.x + 12, at.y + ts.y + 6), hex(0x241942, 235));
        dl->AddRect(ImVec2(at.x - 12, at.y - 6), ImVec2(at.x + ts.x + 12, at.y + ts.y + 6), hex(0xffe36d), 0, 0, 2);
        dl->AddText(font, fs, at, f.message_bad ? hex(0xff8a9a) : hex(0xfff9df), f.message.c_str(), nullptr, sw * 0.8f);
    }

    // Version tag, bottom left of the window: cream on a plum plate.
    const std::string version = std::string("KAM'O ") + KAMO_VERSION;
    ImFont* vfont = g_big_font ? g_big_font : ImGui::GetFont();
    const float vs = std::clamp(H * 0.03f, 15.0f, 24.0f);
    const ImVec2 vsz = vfont->CalcTextSizeA(vs, FLT_MAX, 0, version.c_str());
    const ImVec2 vat(std::floor(wp.x + W * 0.02f + 8), std::floor(wp.y + H - H * 0.021f - vsz.y - 4));
    dl->AddRectFilled(ImVec2(vat.x - 8, vat.y - 4), ImVec2(vat.x + vsz.x + 8, vat.y + vsz.y + 4), hex(0x352050, 235), 5);
    dl->AddText(vfont, vs, ImVec2(vat.x + 2, vat.y + 2), hex(0x150c26), version.c_str());
    dl->AddText(vfont, vs, vat, hex(0xfff9df), version.c_str());
    draw_update_bar(f);
}

// Behind every other screen: the menu's sky, side strips and lower scene, a few twinkling pixel
// stars where the title art would be, and a dark veil so panels and text stay readable.
void draw_backdrop(Front& f) {
    if (!menu_art_ready(f)) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth(), H = ImGui::GetWindowHeight();
    sky_gradient(dl, wp, ImVec2(wp.x + W, wp.y + H));
    const float sw = std::floor(std::min(W, H * kStageW / kStageH)), sh = std::floor(sw * kStageH / kStageW);
    const float sx = std::floor(wp.x + (W - sw) / 2), sy = std::floor(wp.y + (H - sh) / 2);
    if (sx > wp.x + 1) {
        if (f.art.wing_left.id)
            dl->AddImage(static_cast<ImTextureID>(f.art.wing_left.id), wp, ImVec2(sx, wp.y + H));
        if (f.art.wing_right.id)
            dl->AddImage(static_cast<ImTextureID>(f.art.wing_right.id), ImVec2(sx + sw, wp.y), ImVec2(wp.x + W, wp.y + H));
    }
    dl->AddImage(static_cast<ImTextureID>(f.art.scene.id), ImVec2(sx, std::floor(sy + sh * 0.46015f)), ImVec2(sx + sw, sy + sh));
    static const float stars[][3] = {{0.08f, 0.10f, 3}, {0.22f, 0.05f, 2}, {0.35f, 0.16f, 2}, {0.52f, 0.07f, 3},
                                     {0.66f, 0.13f, 2}, {0.81f, 0.06f, 3}, {0.93f, 0.17f, 2}, {0.15f, 0.27f, 2},
                                     {0.44f, 0.29f, 2}, {0.74f, 0.25f, 2}, {0.88f, 0.33f, 3}, {0.04f, 0.38f, 2}};
    const Uint32 now = SDL_GetTicks();
    for (int i = 0; i < int(std::size(stars)); ++i) {
        if ((now / 220 + unsigned(i) * 5) % 13 == 0) continue;  // a slow twinkle
        const float px = stars[i][2];
        const ImVec2 c(std::floor(wp.x + W * stars[i][0]), std::floor(wp.y + H * stars[i][1]));
        const ImU32 col = (now / 220 + unsigned(i)) % 9 == 0 ? hex(0xfff29b) : hex(0xffffff, 220);
        dl->AddRectFilled(ImVec2(c.x - px / 2, c.y - px * 1.5f), ImVec2(c.x + px / 2, c.y + px * 1.5f), col);
        dl->AddRectFilled(ImVec2(c.x - px * 1.5f, c.y - px / 2), ImVec2(c.x + px * 1.5f, c.y + px / 2), col);
    }
    dl->AddRectFilled(wp, ImVec2(wp.x + W, wp.y + H), hex(0x120c26, 120));
}

// The previous menu, kept for a copy without the pixel art.
void screen_menu_plain(Front& f) {
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
        g_online_log = f.dir / "netplay" / "online.log";
        online_log("---- KAMO %s online, lobby %s", KAMO_VERSION,
                   f.np && f.np->default_url ? f.np->default_url(f.np->ctx) : "?");
        f.lobby_version = f.options->netplay->build_identity.substr(0, 31);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "KAMO: online unavailable: %s\n", e.what());
        note(f, T("El modo online no está disponible en esta copia.", "Online play is not available in this copy."), true);
        f.screen = Screen::Menu;
        return false;
    }
    return f.np != nullptr;
}

// What a lobby error code means for the player, and what to do about it.
std::string explain_lobby_error(const char* e) {
    auto is = [e](const char* code) { return std::strcmp(e, code) == 0; };
    if (is("bad_password") || is("wrong_password")) return T("Contraseña incorrecta.", "Wrong password.");
    if (is("lobby_full") || is("full")) return T("La sala está llena.", "The room is full.");
    if (is("ice_not_connected") || is("host_relay_unproven"))
        return T("Un jugador no pudo conectarse con el anfitrión. Espera a que el estado de conexión diga que "
                 "todos llegan y pulsa Empezar partida otra vez. Si no llegan, el anfitrión debe activar UPnP "
                 "en su router o abrir su puerto UDP.",
                 "A player could not connect to the host. Wait until the connection status says everyone can "
                 "reach the host, then press Start match again. If they cannot, the host must turn on UPnP on "
                 "the router or forward the UDP port.");
    if (is("missing_endpoints"))
        return T("El servidor no envió las direcciones de la partida. Pulsa Empezar partida otra vez.",
                 "The server sent no match addresses. Press Start match again.");
    if (is("not_ready") || is("not_all_ready"))
        return T("Todos tienen que estar listos.", "Everyone must be ready.");
    if (is("version_mismatch") || is("game_version"))
        return T("Alguien tiene otra versión de KAMO. Todos deben usar la misma.",
                 "Someone has another KAMO version. Everyone needs the same one.");
    return std::string(T("El servidor respondió: ", "The server answered: ")) + e;
}

void lobby_error(Front& f) {
    if (!f.np->last_error) return;
    const char* e = f.np->last_error(f.np->ctx);
    if (!e || !e[0]) return;
    online_log("lobby error: %s", e);
    note(f, explain_lobby_error(e) + "\n" + T("(código: ", "(code: ") + e +
                T(" · detalles en netplay\\online.log)", " · details in netplay\\online.log)"), true);
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
    if (back_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
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
    const bool named = f.name[0] != '\0';
    bool create_pressed;
    if (g_plate) {  // the menu's plate, right-aligned on the same row
        ImGui::SameLine(ImGui::GetWindowWidth() - 268);
        const ImVec2 size(240, std::floor(240.0f * 131 / 586));
        create_pressed = plate_button("##create", T("Crear una sala", "Create a room"),
                                      ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y - 8), size,
                                      size.y * 0.42f, named && online);
    } else {
        ImGui::SameLine(ImGui::GetWindowWidth() - 228);
        create_pressed = button(T("Crear una sala", "Create a room"), 200, named && online);
    }
    if (create_pressed) {
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
    panel_border();
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (back_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
}

void screen_create(Front& f) {
    const auto* np = f.np;
    heading(T("Crear una sala", "Create a room"));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x(560));
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
    panel_border();
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
    if (back_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Online; }
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

// ---- Kirby colours --------------------------------------------------------------------------
// Each player's Kirby colour travels to the room as a tagged chat line (KAMO hides them). Every
// player sends theirs on entering and again whenever the player count changes, since a player
// who joins later starts with an empty chat ring.
constexpr const char* kColorTag = "#kamo-color ";

struct ColorChoice { const char* es; const char* en; std::uint32_t rgb; };
const ColorChoice kColorChoices[] = {
    {"Original", "Original", kOriginalColor}, {"Azul", "Blue", 0x3A7BFF},   {"Morado", "Purple", 0x9B59FF},
    {"Cian", "Cyan", 0x22D3EE},              {"Lima", "Lime", 0x9BE22D},   {"Naranja", "Orange", 0xFF8A1E},
    {"Dorado", "Gold", 0xFFC83A},            {"Vino", "Wine", 0xA3133A},   {"Negro", "Black", 0x2A2A35},
    {"Blanco", "White", 0xF2F2F2},           {"Gris", "Grey", 0x8C8C9A},   {"Chicle", "Bubblegum", 0xFF5FA8},
};

void read_room_colors(Front& f) {
    const auto* np = f.np;
    if (!np->chat_count || !np->chat_get) return;
    const size_t tag = std::strlen(kColorTag);
    for (int i = 0, n = np->chat_count(np->ctx); i < n; ++i) {
        RecompLauncherCNetplayChatMessage m{};
        if (!np->chat_get(np->ctx, i, &m) || !m.from[0]) continue;
        if (std::strncmp(m.text, kColorTag, tag) == 0) f.kirby_colors[m.from] = parse_kirby_color(m.text + tag);
    }
}

void share_my_color(Front& f, int count) {
    const auto* np = f.np;
    const std::uint32_t mine = parse_kirby_color(f.settings.kirby_color);
    if (!np->chat_send || (count == f.color_sent_count && mine == f.color_sent)) return;
    f.color_sent_count = count;
    f.color_sent = mine;
    np->chat_send(np->ctx, (std::string(kColorTag) + kirby_color_text(mine)).c_str());
}

// A small colour swatch at the cursor, on a line of the frame height.
void color_swatch(std::uint32_t rgb, float side = 14) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetFrameHeight();
    const ImVec2 a(std::floor(at.x), std::floor(at.y + (line - side) / 2));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255), 3);
    dl->AddRect(a, ImVec2(a.x + side, a.y + side), IM_COL32(255, 249, 223, 160), 3, 0, 1);
    ImGui::Dummy(ImVec2(side, line));
}

// The puffball of the update bar, drawn in `rgb` (kOriginalColor keeps its pink): the preview
// of "Your Kirby" in Options.
void draw_puff_colored(ImDrawList* dl, ImVec2 at, float px, int frame, std::uint32_t rgb) {
    for (int y = 0; y < kPuffSize; ++y)
        for (int x = 0; x < kPuffSize; ++x) {
            const char k = kPuff[frame][y][x];
            ImU32 c = puff_color(k);
            if (!c) continue;
            if (rgb != kOriginalColor && (k == 'p' || k == 'l' || k == 'c' || k == 'r' || k == 'R')) {
                const float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
                const float shade = k == 'l' ? 1.35f : k == 'c' ? 0.8f : k == 'r' ? 0.62f : k == 'R' ? 0.45f : 1.0f;
                auto ch = [&](float v) { return int(std::clamp(v * shade + (k == 'l' ? 0.12f : 0.0f), 0.0f, 1.0f) * 255); };
                c = IM_COL32(ch(r), ch(g), ch(b), 255);
            }
            const ImVec2 a(std::floor(at.x + x * px), std::floor(at.y + y * px));
            dl->AddRectFilled(a, ImVec2(a.x + px, a.y + px), c);
        }
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
        if (std::strncmp(m.text, kColorTag, std::strlen(kColorTag)) == 0) continue;  // shared colours
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
    panel_border();
}

// One line on the room's connection, from recomp-net's host relay: in rooms of three or more
// the host carries the match, so its UDP port must be reachable by every guest. recomp-ui's own
// relay_status says nothing in rooms the server marks for ICE, which this build cannot run.
std::string connection_status(Front& f, bool& problem) {
    RNetHostRelayStatus st{};
    char buf[320];
    problem = false;
    if (rnet_lobby_host_relay_status(&st) && st.role == 1) {
        int guests = 0, proven = 0;
        for (int i = 0, n = rnet_lobby_member_count(); i < n; ++i) {
            RNetLobbyMember m{};
            if (!rnet_lobby_member_get(i, &m) || m.is_spectator || rnet_lobby_member_is_host(&m)) continue;
            ++guests;
            if (!std::strcmp(m.path, "direct") && m.path_fresh) ++proven;
        }
        if (!st.port.done) {
            std::snprintf(buf, sizeof(buf), T("abriendo tu puerto UDP %u...", "opening your UDP port %u..."),
                          unsigned(st.port.local_port));
        } else if (!st.port.endpoint[0]) {
            problem = true;
            std::snprintf(buf, sizeof(buf),
                          T("no se pudo abrir tu puerto UDP %u (%s). Activa UPnP en tu router o abre ese puerto, "
                            "o los demás no podrán entrar a la partida.",
                            "your UDP port %u could not be opened (%s). Turn on UPnP on your router or forward "
                            "that port, or the others cannot join the match."),
                          unsigned(st.port.local_port), st.port.detail);
        } else {
            problem = guests > 0 && proven < guests;
            std::snprintf(buf, sizeof(buf),
                          T("llevas la partida en el puerto UDP %u (%s). Te alcanzan %d de %d jugadores.",
                            "you carry the match on UDP port %u (%s). %d of %d players reach you."),
                          unsigned(st.port.local_port), st.port.how[0] ? st.port.how : "?", proven, guests);
        }
        return buf;
    }
    if (rnet_lobby_host_relay_status(&st) && st.role == 2) {
        if (!std::strcmp(st.last_report, "direct")) return T("llegas al anfitrión.", "you reach the host.");
        if (!std::strcmp(st.last_report, "fail")) {
            problem = true;
            return T("no llegas al anfitrión. El anfitrión debe activar UPnP o abrir su puerto UDP.",
                     "you cannot reach the host. The host must turn on UPnP or forward the UDP port.");
        }
        return T("comprobando si llegas al anfitrión...", "checking whether you reach the host...");
    }
    // Not a host-relay room (two seats go through the server): recomp-ui's text, if any.
    if (f.np->relay_status) {
        char status[256] = {};
        if (f.np->relay_status(f.np->ctx, status, sizeof(status)) && status[0]) return status;
    }
    return {};
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
    share_my_color(f, np->member_count(np->ctx));
    read_room_colors(f);
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
                // Developer switch: KAMO_LOCAL_PEER=1 runs several windows of one PC as separate
                // players. Guests dial the host on 127.0.0.1 (the lobby hands out the public
                // address, which needs NAT hairpinning) from a port of their own (guests on one
                // PC would otherwise all pick the same free port when they joined).
                if (const char* lp = SDL_getenv("KAMO_LOCAL_PEER"); lp && lp[0] == '1') {
                    auto& n = *f.options->netplay;
                    if (!n.peer_endpoint.empty() && n.local_seat != 0) {
                        n.peer_endpoint = "127.0.0.1" + n.peer_endpoint.substr(n.peer_endpoint.rfind(':'));
                        n.bind_endpoint = "0.0.0.0:" + std::to_string(free_udp_port(7790 + int(n.local_seat)));
                        online_log("dev KAMO_LOCAL_PEER: dialing the host on this PC");
                    }
                }
                f.options->netplay->log_line = [](const std::string& line) { online_log("%s", line.c_str()); };
                {   // Names and colours by cable port: lobby seat s plays cable port seat_machine[s].
                    KirbyLooks looks;
                    looks.show_names = f.settings.show_names;
                    looks.colors.fill(kOriginalColor);
                    auto& n = *f.options->netplay;
                    std::string summary;
                    for (int s = 0, members = np->member_count(np->ctx); s < members && s < int(n.player_count); ++s) {
                        RecompLauncherCNetplayMember m{};
                        if (!np->member_get(np->ctx, s, &m) || m.is_spectator) continue;
                        const unsigned port = n.seat_machine[s];
                        if (port >= unsigned(kKirbyPorts)) continue;
                        looks.names[port] = std::string(m.display_name).substr(0, 16);
                        if (m.is_local) looks.colors[port] = parse_kirby_color(f.settings.kirby_color);
                        else if (const auto it = f.kirby_colors.find(m.display_name); it != f.kirby_colors.end())
                            looks.colors[port] = it->second;
                        summary += " P" + std::to_string(port + 1) + "=" + m.display_name + "/" + kirby_color_text(looks.colors[port]);
                    }
                    install_kirby_looks(n, looks);
                    online_log("kirby looks:%s (names %s)", summary.c_str(), looks.show_names ? "on" : "off");
                }
                const auto& l = *f.options->netplay;
                online_log("launch: %u players, seat %u, route %s, bind %s, peer %s, delay %u, %s%s",
                           l.player_count, l.local_seat, transport_name(gbarecomp::gba_netplay_transport(l)),
                           short_endpoint(l.bind_endpoint).c_str(), short_endpoint(l.peer_endpoint).c_str(),
                           l.input_delay, l.rollback ? "rollback" : "delay-sync",
                           l.resume_path.empty() ? "" : ", resuming the saved match");
                f.start_game = true;
                return;
            } catch (const std::exception& e) {
                online_log("launch refused by KAMO: %s", e.what());
                note(f, std::string(T("No se pudo empezar la partida: ", "The match could not start: ")) + e.what() +
                            T(" (detalles en netplay\\online.log)", " (details in netplay\\online.log)"), true);
            }
        }
    }

    const bool host = np->is_host(np->ctx) != 0;
    const int count = np->member_count(np->ctx);
    const int max = np->lobby_max_slots ? np->lobby_max_slots(np->ctx) : 4;
    if (!f.logged_in_room) {
        f.logged_in_room = true;
        online_log("in room \"%s\" as %s, %d of %d seats", f.room_title.c_str(), host ? "host" : "guest", count, max);
    }
    if (count != f.logged_count) {
        f.logged_count = count;
        std::string names;
        for (int i = 0; i < count; ++i) {
            RecompLauncherCNetplayMember m{};
            if (!np->member_get(np->ctx, i, &m)) continue;
            names += std::string(i ? ", " : "") + m.display_name + (m.is_host ? " (host)" : "");
        }
        online_log("players %d/%d: %s", count, max, names.c_str());
    }
    heading(f.room_title.c_str());
    char sub[96];
    std::snprintf(sub, sizeof(sub), T("%d de %d jugadores", "%d of %d players"), count, max);
    centered_text(sub, kSoftInk);
    ImGui::Spacing();

    int slowest = -1;
    std::vector<int> rtts;
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
            if (!m.is_local) rtts.push_back(m.latency_ms);
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
            {
                std::uint32_t color = kOriginalColor;
                if (m.is_local) color = parse_kirby_color(f.settings.kirby_color);
                else if (const auto it = f.kirby_colors.find(m.display_name); it != f.kirby_colors.end()) color = it->second;
                if (color != kOriginalColor) { color_swatch(color); ImGui::SameLine(0, 6); }
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
    panel_border();
    if (has_chat) {
        ImGui::SameLine();
        draw_chat(f, host_name, panel_h);
    }

    // The route the match will take: whether the host's port is open and which guests reach it.
    {
        bool problem = false;
        const std::string status = connection_status(f, problem);
        if (!status.empty()) {
            if (f.relay_seen != status) { f.relay_seen = status; online_log("connection: %s", status.c_str()); }
            ImGui::PushTextWrapPos(0);
            ImGui::PushStyleColor(ImGuiCol_Text, problem ? kBad : kSoftInk);
            ImGui::TextUnformatted((std::string(T("Conexión: ", "Connection: ")) + status).c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
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
            const bool fixed = f.settings.input_delay > 0;
            const StartDelay sd = start_delay(rtts);
            const int delay = fixed ? f.settings.input_delay : sd.frames;
            set_engine_auto_delay(!fixed);
            if (np->input_delay_set) np->input_delay_set(np->ctx, delay);
            if (fixed) online_log("input delay %d, fixed in Options (auto-adjust off)", delay);
            else online_log("input delay starts at %d (slowest input path ~%d ms one way; auto-adjust on)",
                            delay, sd.path_ms);
            RecompLauncherCSettings settings{};
            online_log("start pressed: %d players, input delay %d (slowest ping %d ms)", count, delay, slowest);
            if (np->request_start(np->ctx, &settings) != 0) {
                online_log("start refused locally (not everyone ready?)");
                note(f, T("No se pudo empezar. ¿Están todos listos?", "Could not start. Is everyone ready?"), true);
            }
        }
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", T("El anfitrión empieza la partida.", "The host starts the match."));
    }
    if (host && count >= 2) {
        char line[160];
        if (f.settings.input_delay > 0)
            std::snprintf(line, sizeof(line), T("Retraso de entrada: %d fotogramas (fijo en Opciones).",
                                                "Input delay: %d frames (fixed in Options)."), f.settings.input_delay);
        else
            std::snprintf(line, sizeof(line), T("Retraso de entrada: empieza en %d fotogramas y se ajusta solo.",
                                                "Input delay: starts at %d frames and adjusts by itself."),
                          start_delay(rtts).frames);
        centered_text(line, kSoftInk);
    }
    if (host && !(count >= 2 && np->all_ready(np->ctx)))
        centered_text(T("La partida empieza cuando todos (2 o más) estén listos.",
                        "The match can start once everyone (2 or more) is ready."), kSoftInk);
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (back_button(T("Salir de la sala", "Leave room"), 200)) {
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
                {"F", "Mostrar FPS (y la conexión, online)", "Show FPS (and the connection, online)"},
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
                                       "vean lo mismo. Automático (recomendado) empieza según el ping y se ajusta solo "
                                       "durante la partida. Un número fijo desactiva ese ajuste: menos = responde antes, "
                                       "más = menos tirones. Lo decide el anfitrión.",
                                       "Input delay: how many frames button presses wait so everyone sees the same "
                                       "game. Automatic (recommended) starts from the ping and adjusts by itself while "
                                       "playing. A fixed number turns that off: lower responds sooner, higher stutters "
                                       "less. The host decides."));
            ImGui::TextDisabled("%s", T("Mientras juegas online, F muestra la conexión: ping, retraso y rollback.",
                                        "While playing online, F shows the connection: ping, delay and rollback."));
            const char* items[] = {"Auto", "2", "3", "4", "5", "6", "7", "8"};
            int index = f.settings.input_delay ? f.settings.input_delay - 1 : 0;
            ImGui::SetNextItemWidth(200);
            if (ImGui::Combo(T("Retraso", "Delay"), &index, items, 8)) {
                f.settings.input_delay = index ? index + 1 : 0;
                changed = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Tu Kirby", "Your Kirby"))) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", T("Tu color en las partidas online: todos los jugadores ven a tu Kirby así. "
                                       "Cada uno elige el suyo.",
                                       "Your colour in online matches: every player sees your Kirby like this. "
                                       "Everyone picks their own."));
            const std::uint32_t current = parse_kirby_color(f.settings.kirby_color);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 preview = ImGui::GetCursorScreenPos();
            draw_puff_colored(dl, ImVec2(preview.x + 8, preview.y + 4), 5, (SDL_GetTicks() / 400) % 2, current);
            ImGui::Dummy(ImVec2(100, 88));
            ImGui::SameLine();
            ImGui::BeginGroup();
            int column = 0;
            for (const auto& choice : kColorChoices) {
                const bool selected = choice.rgb == current;
                ImGui::PushID(choice.en);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##c", ImVec2(116, 34))) {
                    f.settings.kirby_color = choice.rgb == kOriginalColor ? "" : kirby_color_text(choice.rgb);
                    changed = true;
                }
                const bool hovered = ImGui::IsItemHovered();
                dl->AddRectFilled(at, ImVec2(at.x + 116, at.y + 34), ImGui::ColorConvertFloat4ToU32(hovered ? kSoftHover : kSoft), 6);
                if (selected) dl->AddRect(at, ImVec2(at.x + 116, at.y + 34), IM_COL32(255, 227, 109, 255), 6, 0, 2);
                const std::uint32_t sw = choice.rgb == kOriginalColor ? 0xFF8CC8 : choice.rgb;
                dl->AddRectFilled(ImVec2(at.x + 8, at.y + 9), ImVec2(at.x + 24, at.y + 25),
                                  IM_COL32((sw >> 16) & 255, (sw >> 8) & 255, sw & 255, 255), 3);
                dl->AddText(ImVec2(at.x + 32, at.y + 6), ImGui::ColorConvertFloat4ToU32(kInk), T(choice.es, choice.en));
                ImGui::PopID();
                if (++column % 4) ImGui::SameLine();
            }
            ImGui::EndGroup();
            ImGui::Spacing();
            if (ImGui::Checkbox(T("Mostrar los nombres sobre los Kirby (online)", "Show names above the Kirbys (online)"),
                                &f.settings.show_names))
                changed = true;
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
    panel_border();
    if (changed) save_settings(f.dir, f.settings);
    draw_message(f);
    ImGui::SetCursorPos(ImVec2(28, ImGui::GetWindowHeight() - 62));
    if (back_button(T("Volver", "Back"), 140)) { f.message.clear(); f.screen = Screen::Menu; }
}

}  // namespace

bool run_frontend(std::vector<std::string>& args, gbarecomp::RunOptions& options,
                  const std::filesystem::path& dir) {
    detect_language();
    Front f;
    f.dir = dir;
    f.options = &options;
    f.settings = load_settings(dir);
    // Developer switch: KAMO_KIRBY_COLOR=RRGGBB gives this window its own Kirby colour (several
    // windows of one PC share kamo.ini). Not saved unless Options changes it.
    if (const char* kc = SDL_getenv("KAMO_KIRBY_COLOR"); kc && kc[0])
        f.settings.kirby_color = std::strlen(kc) == 6 ? kc : "";
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
    {
        const auto art = dir / "assets" / "kamo" / "menu";
        f.art.hero = load_png(art / "hero.png", true);
        f.art.scene = load_png(art / "scene.png", true);
        f.art.wing_left = load_png(art / "wing-left.png", true);
        f.art.wing_right = load_png(art / "wing-right.png", true);
        f.art.button = load_png(art / "button.png", true);
        if (menu_art_ready(f)) {
            g_plate = f.art.button.id;
            apply_night_style();
        }
    }

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
        if (f.screen != Screen::Menu) draw_backdrop(f);
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
    for (Texture* t : {&f.art.hero, &f.art.scene, &f.art.wing_left, &f.art.wing_right, &f.art.button})
        if (t->id) glDeleteTextures(1, &t->id);
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

void close_online(int result) {
    if (!g_lobby_opened) return;
    g_lobby_opened = false;
    online_log("match ended (exit code %d); leaving the lobby", result);
    recomp_netplay_host_disconnect();
}

}  // namespace kamo
