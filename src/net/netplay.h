#pragma once
// Netplay session: input-based lockstep over ENet (see docs/MULTIPLAYER.md).
//
// Every PC runs all consoles of the session joined by the in-process link cable; this class only
// moves button input (and periodic state hashes) between the players. The host is player 1 and
// relays every client's messages to the other clients (star topology).
//
// Input for frame f is pressed `delay` frames earlier and sent immediately, so it normally arrives
// before frame f is due. A PC runs frame f only once it has every player's input for f.
// No SDL dependency.

#include <cstdint>
#include <memory>
#include <string>

namespace kamo::net {

inline constexpr std::uint16_t kDefaultPort = 7845;
inline constexpr int kMaxPlayers = 4;

struct Config {
    bool host = false;
    std::string address;            // client: host name or IP
    std::uint16_t port = kDefaultPort;
    int players = 2;                // host: players to wait for (2-4)
    int delay = 3;                  // host: input delay in frames
    std::string version;            // must match on every PC
    std::string rom_sha1;           // must match on every PC
    std::string name;               // shown to the others
    bool loopback_only = false;     // host: listen on 127.0.0.1 only (local tests)
};

enum class State { Idle, Connecting, Waiting, Running, Failed, Ended };

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    bool start(const Config& cfg, std::string* error);
    // Sends and receives pending messages. Call often (at least once per frame).
    void poll();
    void stop();

    State state() const;
    const std::string& message() const;  // human-readable status or failure reason
    int local_slot() const;              // 0 = player 1
    int players() const;
    int delay() const;
    int connected() const;               // players currently in the session (host: incl. itself)
    int ping_ms() const;                 // to the host (client) or the worst client (host)

    // Local input for `frame` (normally current frame + delay()). Call once per frame, in order.
    void submit_input(std::uint32_t frame, std::uint16_t keys);
    // True when every player's input for `frame` is known.
    bool ready(std::uint32_t frame) const;
    std::uint16_t input(int slot, std::uint32_t frame) const;

    // State hash of all consoles after `frame`; compared with the other players' hashes.
    void submit_hash(std::uint32_t frame, std::uint64_t hash);
    // First frame whose hashes differed, or -1.
    long long desync_frame() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace kamo::net
