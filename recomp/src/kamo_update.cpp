#include "kamo_update.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#endif

#ifndef KAMO_VERSION
#define KAMO_VERSION "0.0.0"
#endif

namespace kamo {
namespace {

constexpr const char* kLatestApi = "https://api.github.com/repos/OmegaElMegadivo/KAMO/releases/latest";
constexpr const char* kReleasePage = "https://github.com/OmegaElMegadivo/KAMO/releases/latest";
constexpr const char* kDownloadPrefix = "https://github.com/OmegaElMegadivo/KAMO/releases/download/";

std::vector<int> version_numbers(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<int> parts;
    int value = -1;
    for (char c : v) {
        if (c >= '0' && c <= '9') { value = (value < 0 ? 0 : value * 10) + (c - '0'); continue; }
        if (value >= 0) parts.push_back(value);
        value = -1;
        if (c != '.') break;  // "-alpha" and the like end the number
    }
    if (value >= 0) parts.push_back(value);
    return parts;
}

// The string value of the first `"key": "..."` in a JSON text at or after `from`.
std::string json_string(const std::string& json, const std::string& key, std::size_t from = 0,
                        std::size_t* end = nullptr) {
    const auto k = json.find("\"" + key + "\"", from);
    if (k == std::string::npos) return {};
    const auto open = json.find('"', json.find(':', k) + 1);
    if (open == std::string::npos) return {};
    std::string out;
    std::size_t i = open + 1;
    for (; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;
        out += json[i];
    }
    if (end) *end = i;
    return out;
}

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// HTTPS GET. Writes the body to `out` (string) or `file`; follows redirects.
bool http_get(const std::string& url, std::string* out, std::ofstream* file,
              std::atomic<int>* progress, std::string* error) {
    const std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) { *error = "bad url"; return false; }
    HINTERNET session = WinHttpOpen(L"KAMO-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { *error = "network unavailable"; return false; }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);  // never hold the menu for long
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = request &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", DWORD(-1),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, size = sizeof(status);
    if (ok) {
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        ok = status == 200;
        if (!ok) *error = "server answered " + std::to_string(status);
    } else {
        *error = "no connection";
    }
    DWORD total = 0;
    size = sizeof(total);
    if (ok)
        WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &total, &size, WINHTTP_NO_HEADER_INDEX);
    std::uint64_t received = 0;
    std::vector<char> buffer(1 << 16);
    while (ok) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read)) {
            ok = false; *error = "download interrupted"; break;
        }
        if (read == 0) break;
        if (out) out->append(buffer.data(), read);
        if (file && !file->write(buffer.data(), read)) { ok = false; *error = "cannot write"; break; }
        received += read;
        if (progress && total) progress->store(int(received * 100 / total));
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}
#endif

}  // namespace

bool version_newer(const std::string& candidate, const std::string& current) {
    const auto a = version_numbers(candidate), b = version_numbers(current);
    if (a.empty()) return false;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

struct Updater::Impl {
    mutable std::mutex lock;
    std::atomic<State> state{State::Checking};
    std::atomic<int> progress{0};
    UpdateInfo info;
    std::string error;
    std::filesystem::path installer;
    std::thread worker;
    ~Impl() { if (worker.joinable()) worker.join(); }
};

Updater::~Updater() { delete impl_; }

void Updater::start(bool installed) {
    installed_ = installed;
    impl_ = new Impl;
    impl_->worker = std::thread([impl = impl_] {
#if defined(_WIN32)
        std::string body, error;
        if (!http_get(kLatestApi, &body, nullptr, nullptr, &error)) {
            std::lock_guard<std::mutex> g(impl->lock);
            impl->error = error;
            impl->state = State::Failed;
            return;
        }
        UpdateInfo info;
        info.version = json_string(body, "tag_name");
        if (!info.version.empty() && (info.version[0] == 'v' || info.version[0] == 'V'))
            info.version.erase(0, 1);
        info.page_url = kReleasePage;
        for (std::size_t at = 0;;) {
            std::size_t end = 0;
            const auto url = json_string(body, "browser_download_url", at, &end);
            if (url.empty()) break;
            // Only an installer attached to this repository's own releases.
            if (url.rfind(kDownloadPrefix, 0) == 0 &&
                url.find("KAMO-Setup-") != std::string::npos && url.size() > 4 &&
                url.compare(url.size() - 4, 4, ".exe") == 0)
                info.installer_url = url;
            at = end;
        }
        std::lock_guard<std::mutex> g(impl->lock);
        impl->info = info;
        impl->state = version_newer(info.version, KAMO_VERSION) ? State::Available : State::UpToDate;
        std::fprintf(stderr, "KAMO: latest release %s, this is %s\n", info.version.c_str(), KAMO_VERSION);
#else
        impl->state = State::UpToDate;
#endif
    });
}

Updater::State Updater::state() const { return impl_ ? impl_->state.load() : State::UpToDate; }
int Updater::progress() const { return impl_ ? impl_->progress.load() : 0; }
UpdateInfo Updater::info() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> g(impl_->lock);
    return impl_->info;
}
std::string Updater::error() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> g(impl_->lock);
    return impl_->error;
}

void Updater::download() {
    if (!impl_ || impl_->state != State::Available) return;
    const auto info = this->info();
    if (info.installer_url.empty()) { open_release_page(); return; }
    impl_->state = State::Downloading;
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->worker = std::thread([impl = impl_, info] {
#if defined(_WIN32)
        std::error_code ec;
        const auto path = std::filesystem::temp_directory_path(ec) / ("KAMO-Setup-" + info.version + ".exe");
        std::string error;
        bool ok;
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            ok = file && http_get(info.installer_url, nullptr, &file, &impl->progress, &error);
        }
        std::lock_guard<std::mutex> g(impl->lock);
        if (ok) { impl->installer = path; impl->state = State::Ready; }
        else { impl->error = error.empty() ? "download failed" : error; impl->state = State::Failed; }
#endif
    });
}

bool Updater::launch_installer() {
#if defined(_WIN32)
    if (!impl_ || impl_->state != State::Ready) return false;
    // The installer closes nothing by itself while KAMO runs; KAMO quits right after this.
    const auto params = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
    const auto result = ShellExecuteW(nullptr, L"open", impl_->installer.wstring().c_str(), params,
                                      nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    return false;
#endif
}

void Updater::open_release_page() const {
#if defined(_WIN32)
    ShellExecuteW(nullptr, L"open", widen(kReleasePage).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

}  // namespace kamo
