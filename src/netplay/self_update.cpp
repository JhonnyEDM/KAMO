// KAMO-Netplay self-update. See self_update.h.
#include "netplay/self_update.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <vector>

#include "update/http.h"
#include "update/sha256.h"
#include "update/update_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace kamo::netplay {

namespace {

constexpr const char* kReleases = "https://api.github.com/repos/JhonnyEDM/KAMO/releases?per_page=10";
constexpr const char* kAsset = "KAMO-Netplay.exe";

fs::path exe_path() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return fs::path(buf);
#else
    return {};
#endif
}

// Splits a JSON array into its top-level objects (as text), respecting strings.
std::vector<std::string> top_level_objects(const std::string& json) {
    std::vector<std::string> out;
    int depth = 0;
    bool in_string = false, escape = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (in_string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{') {
            if (depth++ == 0) start = i;
        } else if (c == '}') {
            if (--depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
    }
    return out;
}

}  // namespace

SelfUpdate::SelfUpdate(std::string current_version) : current_(std::move(current_version)) {}
SelfUpdate::~SelfUpdate() { join(); }

void SelfUpdate::join() {
    if (worker_.joinable()) worker_.join();
}

std::string SelfUpdate::version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

std::string SelfUpdate::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

void SelfUpdate::check() {
#ifdef _WIN32
    join();
    state_ = State::Checking;
    worker_ = std::thread([this] {
        update::HttpOptions opt;
        opt.user_agent = "KAMO-Netplay";
        opt.accept = "application/vnd.github+json";
        std::string body, err;
        if (!update::http_get(kReleases, &body, opt, &err)) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = err;
            state_ = State::Failed;
            return;
        }
        auto mine = update::parse_version(current_);
        std::optional<update::Version> best;
        std::string best_tag, url, sha;
        int objects = 0, parsed = 0;
        for (const std::string& obj : top_level_objects(body)) {
            ++objects;
            auto rel = update::parse_release_json(obj);
            if (!rel || rel->draft) continue;
            ++parsed;
            const update::Asset* a = rel->find(kAsset);
            auto v = update::parse_version(rel->tag);
            if (!a || !v) continue;
            if (!best || update::compare_versions(*v, *best) > 0) {
                best = v;
                best_tag = rel->tag;
                url = a->url;
                sha = a->sha256;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (best && mine && update::compare_versions(*best, *mine) > 0) {
            version_ = update::to_string(*best);
            url_ = url;
            sha256_ = sha;
            state_ = State::Available;
        } else {
            // Diagnostics for the update test mode.
            error_ = "releases " + std::to_string(objects) + ", read " + std::to_string(parsed) + ", body " +
                     std::to_string(body.size()) + " bytes";
            state_ = State::UpToDate;
        }
    });
#else
    state_ = State::UpToDate;
#endif
}

void SelfUpdate::install() {
#ifdef _WIN32
    if (state_ != State::Available) return;
    join();
    state_ = State::Downloading;
    progress_ = 0;
    worker_ = std::thread([this] {
        std::string url, sha;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            url = url_;
            sha = sha256_;
        }
        auto fail = [&](const std::string& why) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = why;
            state_ = State::Failed;
        };
        fs::path exe = exe_path();
        if (exe.empty()) return fail("Could not find the running program");
        fs::path stage = exe;
        stage += ".new";
        update::HttpOptions opt;
        opt.user_agent = "KAMO-Netplay";
        opt.progress = [this](std::uint64_t done, std::uint64_t total) {
            if (total > 0) progress_ = float(double(done) / double(total));
        };
        std::string err;
        if (!update::http_download(url, stage, opt, &err)) return fail("Download failed: " + err);
        if (!sha.empty() && kamo::sha256_file_hex(stage) != sha) {
            std::error_code ec;
            fs::remove(stage, ec);
            return fail("The download is damaged (checksum mismatch)");
        }
        fs::path old = exe;
        old += ".old";
        std::error_code ec;
        fs::remove(old, ec);
        fs::rename(exe, old, ec);
        if (ec) return fail("Could not replace the program: " + ec.message());
        fs::rename(stage, exe, ec);
        if (ec) {
            std::error_code ec2;
            fs::rename(old, exe, ec2);
            return fail("Could not install the update: " + ec.message());
        }
        progress_ = 1;
        state_ = State::Ready;
    });
#endif
}

void SelfUpdate::relaunch() {
#ifdef _WIN32
    if (state_ != State::Ready) return;
    std::string target = exe_path().string();
    const char* args[] = {target.c_str(), nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    if (SDL_Process* p = SDL_CreateProcessWithProperties(props)) SDL_DestroyProcess(p);
    SDL_DestroyProperties(props);
#endif
}

void SelfUpdate::cleanup() {
    fs::path exe = exe_path();
    if (exe.empty()) return;
    fs::path old = exe;
    old += ".old";
    std::error_code ec;
    fs::remove(old, ec);
}

}  // namespace kamo::netplay
