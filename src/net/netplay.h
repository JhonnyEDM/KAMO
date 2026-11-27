#pragma once
// Netplay session: input-based lockstep over ENet (see docs/MULTIPLAYER.md).
//
// Every PC runs all consoles of the session joined by the in-process link cable; this class only
// moves button input (and periodic state hashes) between the players. The host is player 1 and
// relays every client's messages to the other clients (star topology).
//
// Input for frame f is pressed `delay` frames earlier and sent immediately, so it normally arrives
// before frame f is due. A PC runs frame f only once it has every player's input for f.
//
// With an automatic delay the host measures the round trip to every player for about a second
// once everyone has joined, then picks the smallest delay that covers it.
//
// Every input message also carries the sender's current frame, so each PC can tell whether it
// runs ahead of the others (frames_ahead) and slow down slightly instead of stalling.
// No SDL dependency.

#include <cstdint>
#include <memory>
#include <string>

namespace kamo::net {

inline constexpr std::uint16_t kDefaultPort = 7845;
inline constexpr int kMaxPlayers = 4;
inline constexpr int kMaxDelay = 8;

struct Config {
    bool host = false;
    std::string address;            // client: host name or IP
    std::uint16_t port = kDefaultPort;
    int players = 2;                // host: players to wait for (2-4)
    int delay = 0;                  // host: input delay in frames, 0 = automatic from ping
    std::string version;            // must match on every PC
    std::string rom_sha1;           // must match on every PC
    std::string name;               // shown to the others
    bool loopback_only = false;     // host: listen on 127.0.0.1 only (local tests)
};

enum class State { Idle, Connecting, Waiting, Running, Failed, Ended };

// Input delay (frames) that covers a round trip of rtt_ms at ~60 fps, with a small margin.
int delay_for_rtt(int rtt_ms);

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
    int connected() const;               // players currently in the session (incl. this one)
    int ping_ms() const;                 // to the host (client) or the worst client (host)

    // Local input for `frame` (normally my_frame + delay()), sent with this PC's current frame.
    // Call once per frame, in order.
    void submit_input(std::uint32_t frame, std::uint16_t keys, std::uint32_t my_frame);
    // True when every player's input for `frame` is known.
    bool ready(std::uint32_t frame) const;
    std::uint16_t input(int slot, std::uint32_t frame) const;
    // How many frames this PC runs ahead of the slowest other player (negative: behind),
    // estimated from their last reported frame, the time since and half the round trip.
    double frames_ahead(std::uint32_t my_frame, double frame_seconds) const;

    // State hash of all consoles after `frame`; compared with the other players' hashes.
    void submit_hash(std::uint32_t frame, std::uint64_t hash);
    // First frame whose hashes differed, or -1.
    long long desync_frame() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace kamo::net
