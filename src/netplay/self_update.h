#pragma once
// KAMO-Netplay self-update from this repository's GitHub releases, test builds included.
// The newest release with a "KAMO-Netplay.exe" asset and a higher version is offered; installing
// downloads it next to the running program, swaps the files (Windows lets a running .exe be
// renamed) and starts the new one. Windows only; elsewhere the check reports nothing.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace kamo::netplay {

class SelfUpdate {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };

    explicit SelfUpdate(std::string current_version);
    ~SelfUpdate();

    void check();      // background thread
    void install();    // background thread; Ready when the new program is in place
    void relaunch();   // starts the new program (call after Ready, then quit)
    // Removes the program left behind by the previous update.
    static void cleanup();

    State state() const { return state_.load(); }
    std::string version() const;   // available version
    std::string error() const;
    float progress() const { return progress_.load(); }

private:
    void join();
    std::string current_;
    std::atomic<State> state_{State::Idle};
    std::atomic<float> progress_{0};
    mutable std::mutex mutex_;
    std::string version_, url_, sha256_, error_;
    std::thread worker_;
};

}  // namespace kamo::netplay
