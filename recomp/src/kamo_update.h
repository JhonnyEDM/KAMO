#pragma once
// Automatic updates from the GitHub releases of OmegaElMegadivo/KAMO.
//
// At start the menu asks GitHub for the latest release in the background. When it is newer than
// this build, the menu offers it: KAMO downloads the release's installer, starts it silently
// (it replaces the installed files and opens KAMO again) and quits. A copy of KAMO that was not
// installed with the installer opens the release page instead.

#include <filesystem>
#include <string>

namespace kamo {

struct UpdateInfo {
    std::string version;        // "0.3.1"
    std::string installer_url;  // KAMO-Setup-<version>.exe
    std::string page_url;       // the release page
};

class Updater {
public:
    enum class State { Checking, UpToDate, Available, Downloading, Ready, Failed };

    // Starts the background check. `installed` is true for a copy set up by the installer.
    void start(bool installed);
    State state() const;
    UpdateInfo info() const;
    int progress() const;        // 0..100 while downloading
    std::string error() const;
    bool installed() const { return installed_; }

    // Downloads the installer in the background (state Downloading, then Ready or Failed).
    void download();
    // Runs the downloaded installer silently. The caller quits right after.
    bool launch_installer();
    void open_release_page() const;

    ~Updater();

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool installed_ = false;
};

// "0.3.0" < "0.3.1"; a leading "v" and a "-alpha" style suffix are ignored.
bool version_newer(const std::string& candidate, const std::string& current);

}  // namespace kamo
