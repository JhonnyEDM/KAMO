#pragma once
// Automatic updates from the GitHub releases of OmegaElMegadivo/KAMO.
//
// Each time the menu opens it asks GitHub for the latest release in the background. When it is
// newer than this build, KAMO updates itself, checking every download's size and SHA-256 against
// the release first:
// - an installed copy downloads the release's installer, starts it silently (it replaces the
//   installed files and opens KAMO again) and quits;
// - a portable copy (the zip) downloads the release's zip, unpacks it next to itself, swaps its
//   files for the new ones (the old ones are renamed *.kamo-old and removed on the next start),
//   opens the new KAMO and quits. The player's own files (settings, ROM path, saves, sign-in)
//   are not in the zip and stay as they are.
// When a release lacks the file a copy needs, the release page opens instead.

#include <filesystem>
#include <memory>
#include <cstdint>
#include <string>

namespace kamo {

struct UpdateInfo {
    std::string version;        // "0.3.1"
    std::string installer_url;  // KAMO-Setup-<version>.exe
    std::string page_url;       // the release page
    std::string sha256;         // the installer's digest from the release, lowercase hex ("" if none)
    std::uint64_t size = 0;     // the installer's size from the release (0 if unknown)
    std::string zip_url;        // KAMO-<version>-Windows-x64.zip, for portable copies
    std::string zip_sha256;
    std::uint64_t zip_size = 0;
};

class Updater {
public:
    enum class State { Checking, UpToDate, Available, Downloading, Verifying, Ready, Failed };

    // Starts the background check. `installed` is true for a copy set up by the installer.
    void start(bool installed);
    State state() const;
    UpdateInfo info() const;
    int progress() const;        // 0..100 while downloading
    std::string error() const;
    bool installed() const { return installed_; }
    bool started() const { return impl_ != nullptr; }
    // The latest release carries what this copy needs to update itself (installer or zip).
    bool can_self_update() const;
    // Failed while downloading or verifying the installer (not while checking).
    bool download_failed() const;
    // True from the start of a download until the installer runs: the menu holds the game then.
    bool busy() const;

    // Downloads the installer in the background (state Downloading, then Ready or Failed).
    void download();
    // After a failure: downloads again, or checks again when the check itself failed.
    void retry();
    // Installed copy: runs the downloaded installer silently. Portable copy: swaps in the
    // unpacked files and opens the new KAMO. The caller quits right after a true.
    bool launch_installer();
    void open_release_page() const;

    ~Updater();

private:
    struct Impl;
    // Shared with the worker thread, which is detached when the menu closes so a slow network
    // never holds up starting the game.
    std::shared_ptr<Impl> impl_;
    bool installed_ = false;
};

// "0.3.0" < "0.3.1"; a leading "v" and a "-alpha" style suffix are ignored.
bool version_newer(const std::string& candidate, const std::string& current);

// Runs this executable again with `argument` and waits for it; returns its exit code, or -1
// when it could not be started.
int run_self_and_wait(const char* argument);

}  // namespace kamo
