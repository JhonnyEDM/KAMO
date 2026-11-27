// Netplay session over ENet. See netplay.h.
#include "net/netplay.h"

#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

namespace kamo::net {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint16_t kProtocol = 2;
constexpr std::uint8_t kChannels = 1;
constexpr auto kMeasureTime = std::chrono::milliseconds(1200);  // ping phase before the start
constexpr auto kPingInterval = std::chrono::milliseconds(50);

enum Msg : std::uint8_t {
    kHello = 1,    // client -> host: protocol, version, rom sha1, name
    kReject = 2,   // host -> client: reason
    kWelcome = 3,  // host -> client: slot, players expected
    kStart = 4,    // host -> client: players, delay, this client's round trip (ms)
    kInput = 5,    // any: slot, frame, keys, sender's current frame (host relays)
    kHash = 6,     // any: slot, frame, hash (host relays)
    kLobby = 7,    // host -> all: players connected so far
    kPing = 8,     // host -> client: id
    kPong = 9,     // client -> host: id
};

struct Writer {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(std::uint16_t v) { u8(std::uint8_t(v)); u8(std::uint8_t(v >> 8)); }
    void u32(std::uint32_t v) { u16(std::uint16_t(v)); u16(std::uint16_t(v >> 16)); }
    void u64(std::uint64_t v) { u32(std::uint32_t(v)); u32(std::uint32_t(v >> 32)); }
    void str(const std::string& s) {
        std::size_t n = std::min<std::size_t>(s.size(), 255);
        u8(std::uint8_t(n));
        b.insert(b.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
};

struct Reader {
    const std::uint8_t* p;
    std::size_t n, i = 0;
    bool ok = true;
    std::uint8_t u8() {
        if (i >= n) { ok = false; return 0; }
        return p[i++];
    }
    std::uint16_t u16() { std::uint16_t lo = u8(); return std::uint16_t(lo | (u8() << 8)); }
    std::uint32_t u32() { std::uint32_t lo = u16(); return lo | (std::uint32_t(u16()) << 16); }
    std::uint64_t u64() { std::uint64_t lo = u32(); return lo | (std::uint64_t(u32()) << 32); }
    std::string str() {
        std::size_t len = u8();
        if (i + len > n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }
};

void send(ENetPeer* peer, const Writer& w) {
    if (!peer) return;
    ENetPacket* pkt = enet_packet_create(w.b.data(), w.b.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, pkt);
}

int g_enet_users = 0;

}  // namespace

int delay_for_rtt(int rtt_ms) {
    // Input travels one way (half the round trip); add ~10 ms for jitter and frame alignment.
    const double frame_ms = 1000.0 / 59.7275;
    int d = int(std::ceil((std::max(rtt_ms, 0) * 0.5 + 10.0) / frame_ms));
    return std::clamp(d, 1, kMaxDelay);
}

struct Session::Impl {
    Config cfg;
    State state = State::Idle;
    std::string message;
    ENetHost* host = nullptr;
    ENetPeer* server = nullptr;                    // client: connection to the host
    std::array<ENetPeer*, kMaxPlayers> peers{};    // host: client per slot (slot 0 = host)
    int slot = 0;
    int players = 2;
    int delay = 3;
    int connected = 0;
    std::array<std::vector<std::int32_t>, kMaxPlayers> inputs;  // -1 = not known yet
    std::map<std::uint32_t, std::array<std::optional<std::uint64_t>, kMaxPlayers>> hashes;
    long long desync = -1;

    // Round trips: host measures each client before the start; a client learns its own in kStart.
    std::array<int, kMaxPlayers> rtt_ms{};
    bool measuring = false;
    Clock::time_point measure_end, next_ping;
    std::uint32_t ping_id = 0;
    std::map<std::uint32_t, Clock::time_point> ping_sent;
    std::array<std::vector<int>, kMaxPlayers> samples;

    // Last frame each player reported and when it arrived (for frames_ahead).
    std::array<std::optional<std::uint32_t>, kMaxPlayers> their_frame;
    std::array<Clock::time_point, kMaxPlayers> their_frame_at{};

    void fail(const std::string& why) {
        state = State::Failed;
        message = why;
    }

    void set_input(int s, std::uint32_t frame, std::uint16_t keys) {
        if (s < 0 || s >= kMaxPlayers) return;
        auto& v = inputs[std::size_t(s)];
        if (v.size() <= frame) v.resize(std::size_t(frame) + 1024, -1);
        v[frame] = keys;
    }

    void note_frame(int s, std::uint32_t sender_frame) {
        if (s < 0 || s >= kMaxPlayers) return;
        auto& f = their_frame[std::size_t(s)];
        if (!f || sender_frame >= *f) {
            f = sender_frame;
            their_frame_at[std::size_t(s)] = Clock::now();
        }
    }

    void set_hash(int s, std::uint32_t frame, std::uint64_t h) {
        if (s < 0 || s >= players) return;
        auto& row = hashes[frame];
        row[std::size_t(s)] = h;
        bool all = true;
        for (int i = 0; i < players; ++i) all = all && row[std::size_t(i)].has_value();
        if (!all) return;
        for (int i = 1; i < players; ++i)
            if (*row[std::size_t(i)] != *row[0] && desync < 0) desync = frame;
        hashes.erase(frame);
    }

    void begin(int n, int d) {
        players = n;
        delay = d;
        for (auto& v : inputs) v.assign(4096, -1);
        // The first `delay` frames have no input yet: everyone starts with nothing pressed.
        for (int s = 0; s < players; ++s)
            for (int f = 0; f < delay; ++f) inputs[std::size_t(s)][std::size_t(f)] = 0;
        state = State::Running;
        message = "Running";
    }

    void broadcast_except(const Writer& w, int except_slot) {
        for (int s = 1; s < kMaxPlayers; ++s)
            if (s != except_slot && peers[std::size_t(s)]) send(peers[std::size_t(s)], w);
    }

    // Host: everyone is here and the delay is known.
    void host_start(int d) {
        measuring = false;
        for (int s = 1; s < players; ++s) {
            Writer w;
            w.u8(kStart);
            w.u8(std::uint8_t(players));
            w.u8(std::uint8_t(d));
            w.u16(std::uint16_t(std::clamp(rtt_ms[std::size_t(s)], 0, 65535)));
            send(peers[std::size_t(s)], w);
        }
        begin(players, d);
    }

    void host_lobby_update() {
        connected = 1;
        for (int s = 1; s < kMaxPlayers; ++s)
            if (peers[std::size_t(s)]) ++connected;
        Writer w;
        w.u8(kLobby);
        w.u8(std::uint8_t(connected));
        broadcast_except(w, -1);
        if (state != State::Waiting) return;
        if (connected >= players) {
            if (cfg.delay > 0) {
                host_start(std::clamp(cfg.delay, 1, kMaxDelay));
            } else {
                measuring = true;
                measure_end = Clock::now() + kMeasureTime;
                next_ping = Clock::now();
                for (auto& s : samples) s.clear();
                message = "Everyone is here. Measuring the connection...";
            }
        } else {
            measuring = false;
            message = "Waiting for players: " + std::to_string(connected) + "/" + std::to_string(players);
        }
    }

    void host_measure() {
        if (!measuring) return;
        const auto now = Clock::now();
        if (now >= next_ping) {
            next_ping = now + kPingInterval;
            Writer w;
            w.u8(kPing);
            w.u32(++ping_id);
            ping_sent[ping_id] = now;
            broadcast_except(w, -1);
        }
        if (now < measure_end) return;
        int worst = 0;
        for (int s = 1; s < players; ++s) {
            auto v = samples[std::size_t(s)];
            int rtt = 150;  // no samples: assume a slow connection
            if (!v.empty()) {
                std::sort(v.begin(), v.end());
                rtt = v[v.size() / 2];  // median
            }
            rtt_ms[std::size_t(s)] = rtt;
            worst = std::max(worst, rtt);
        }
        ping_sent.clear();
        host_start(delay_for_rtt(worst));
    }

    void host_receive(ENetPeer* peer, Reader& r) {
        int from = int(reinterpret_cast<std::intptr_t>(peer->data));  // slot, 0 = not joined yet
        std::uint8_t type = r.u8();
        if (type == kHello) {
            std::uint16_t proto = r.u16();
            std::string version = r.str(), sha1 = r.str(), name = r.str();
            if (!r.ok) return;
            std::string reason;
            if (proto != kProtocol || version != cfg.version)
                reason = "Different KAMO version (host has " + cfg.version + ", you have " + version + ")";
            else if (sha1 != cfg.rom_sha1)
                reason = "Different ROM: everyone needs Kirby & The Amazing Mirror (USA)";
            else if (state != State::Waiting || measuring)
                reason = "The session has already started";
            int free_slot = -1;
            for (int s = 1; s < players && free_slot < 0; ++s)
                if (!peers[std::size_t(s)]) free_slot = s;
            if (reason.empty() && free_slot < 0) reason = "The session is full";
            if (!reason.empty()) {
                Writer w;
                w.u8(kReject);
                w.str(reason);
                send(peer, w);
                enet_peer_disconnect_later(peer, 0);
                return;
            }
            peers[std::size_t(free_slot)] = peer;
            peer->data = reinterpret_cast<void*>(std::intptr_t(free_slot));
            Writer w;
            w.u8(kWelcome);
            w.u8(std::uint8_t(free_slot));
            w.u8(std::uint8_t(players));
            send(peer, w);
            host_lobby_update();
            return;
        }
        if (from <= 0) return;
        if (type == kPong) {
            std::uint32_t id = r.u32();
            auto it = ping_sent.find(id);
            if (r.ok && it != ping_sent.end()) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - it->second).count();
                samples[std::size_t(from)].push_back(int(ms));
            }
            return;
        }
        if (state != State::Running) return;
        if (type == kInput) {
            r.u8();  // the sender's slot is taken from the connection, not the message
            std::uint32_t frame = r.u32();
            std::uint16_t keys = r.u16();
            std::uint32_t sender_frame = r.u32();
            if (!r.ok) return;
            set_input(from, frame, keys);
            note_frame(from, sender_frame);
            Writer w;
            w.u8(kInput);
            w.u8(std::uint8_t(from));
            w.u32(frame);
            w.u16(keys);
            w.u32(sender_frame);
            broadcast_except(w, from);
        } else if (type == kHash) {
            r.u8();
            std::uint32_t frame = r.u32();
            std::uint64_t h = r.u64();
            if (!r.ok) return;
            set_hash(from, frame, h);
            Writer w;
            w.u8(kHash);
            w.u8(std::uint8_t(from));
            w.u32(frame);
            w.u64(h);
            broadcast_except(w, from);
        }
    }

    void client_receive(Reader& r) {
        std::uint8_t type = r.u8();
        switch (type) {
            case kReject: fail("Rejected by host: " + r.str()); break;
            case kWelcome:
                slot = r.u8();
                players = r.u8();
                state = State::Waiting;
                message = "Joined as player " + std::to_string(slot + 1) + ", waiting for the others";
                break;
            case kLobby:
                connected = r.u8();
                if (state == State::Waiting)
                    message = "Joined as player " + std::to_string(slot + 1) + ". Players: " +
                              std::to_string(connected) + "/" + std::to_string(players);
                break;
            case kPing: {
                std::uint32_t id = r.u32();
                if (!r.ok) break;
                Writer w;
                w.u8(kPong);
                w.u32(id);
                send(server, w);
                if (state == State::Waiting) message = "Everyone is here. Measuring the connection...";
                break;
            }
            case kStart: {
                int n = r.u8(), d = r.u8();
                int rtt = r.u16();
                if (!r.ok) break;
                for (auto& v : rtt_ms) v = rtt;  // everyone is reached through the host
                connected = n;
                begin(n, d);
                break;
            }
            case kInput: {
                int s = r.u8();
                std::uint32_t frame = r.u32();
                std::uint16_t keys = r.u16();
                std::uint32_t sender_frame = r.u32();
                if (!r.ok) break;
                set_input(s, frame, keys);
                note_frame(s, sender_frame);
                break;
            }
            case kHash: {
                int s = r.u8();
                std::uint32_t frame = r.u32();
                std::uint64_t h = r.u64();
                if (r.ok) set_hash(s, frame, h);
                break;
            }
            default: break;
        }
    }
};

Session::Session() : d_(std::make_unique<Impl>()) {}
Session::~Session() { stop(); }

bool Session::start(const Config& cfg, std::string* error) {
    stop();
    if (g_enet_users == 0 && enet_initialize() != 0) {
        if (error) *error = "Could not initialise networking";
        return false;
    }
    ++g_enet_users;
    Impl& d = *d_;
    d = Impl{};
    d.cfg = cfg;
    auto fail_start = [&](const std::string& why) {
        if (error) *error = why;
        if (d.host) enet_host_destroy(d.host);
        d.host = nullptr;
        if (--g_enet_users == 0) enet_deinitialize();
        return false;
    };
    if (cfg.host) {
        d.players = std::clamp(cfg.players, 2, kMaxPlayers);
        ENetAddress addr{};
        addr.host = ENET_HOST_ANY;
        if (cfg.loopback_only) enet_address_set_host(&addr, "127.0.0.1");
        addr.port = cfg.port;
        d.host = enet_host_create(&addr, kMaxPlayers - 1, kChannels, 0, 0);
        if (!d.host) return fail_start("Could not open port " + std::to_string(cfg.port) + " (already in use?)");
        d.slot = 0;
        d.state = State::Waiting;
        d.host_lobby_update();
    } else {
        d.host = enet_host_create(nullptr, 1, kChannels, 0, 0);
        if (!d.host) return fail_start("Could not create a network socket");
        ENetAddress addr{};
        if (enet_address_set_host(&addr, cfg.address.c_str()) != 0) return fail_start("Unknown host: " + cfg.address);
        addr.port = cfg.port;
        d.server = enet_host_connect(d.host, &addr, kChannels, 0);
        if (!d.server) return fail_start("Could not start the connection");
        d.state = State::Connecting;
        d.message = "Connecting to " + cfg.address + "...";
    }
    return true;
}

void Session::poll() {
    Impl& d = *d_;
    if (!d.host) return;
    ENetEvent ev;
    while (enet_host_service(d.host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (!d.cfg.host) {
                    Writer w;
                    w.u8(kHello);
                    w.u16(kProtocol);
                    w.str(d.cfg.version);
                    w.str(d.cfg.rom_sha1);
                    w.str(d.cfg.name);
                    send(d.server, w);
                    d.message = "Connected. Checking version and ROM...";
                } else {
                    ev.peer->data = nullptr;
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE: {
                Reader r{ev.packet->data, ev.packet->dataLength};
                if (d.cfg.host) d.host_receive(ev.peer, r);
                else d.client_receive(r);
                enet_packet_destroy(ev.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
                if (d.cfg.host) {
                    int s = int(reinterpret_cast<std::intptr_t>(ev.peer->data));
                    if (s > 0 && d.peers[std::size_t(s)] == ev.peer) {
                        d.peers[std::size_t(s)] = nullptr;
                        if (d.state == State::Running) d.fail("Player " + std::to_string(s + 1) + " left the session");
                        else d.host_lobby_update();
                    }
                } else if (d.state != State::Failed) {
                    d.fail(d.state == State::Connecting ? "Could not reach the host" : "The host closed the session");
                }
                break;
            default: break;
        }
    }
    if (d.cfg.host) d.host_measure();
    enet_host_flush(d.host);
}

void Session::stop() {
    Impl& d = *d_;
    if (!d.host) return;
    if (d.server) enet_peer_disconnect_now(d.server, 0);
    for (ENetPeer* p : d.peers)
        if (p) enet_peer_disconnect_now(p, 0);
    enet_host_flush(d.host);
    enet_host_destroy(d.host);
    d.host = nullptr;
    d.server = nullptr;
    d.peers = {};
    if (d.state != State::Failed) d.state = State::Ended;
    if (--g_enet_users == 0) enet_deinitialize();
}

State Session::state() const { return d_->state; }
const std::string& Session::message() const { return d_->message; }
int Session::local_slot() const { return d_->slot; }
int Session::players() const { return d_->players; }
int Session::delay() const { return d_->delay; }
int Session::connected() const { return d_->connected; }

int Session::ping_ms() const {
    const Impl& d = *d_;
    if (d.server) return int(d.server->roundTripTime);
    int worst = 0;
    for (ENetPeer* p : d.peers)
        if (p) worst = std::max(worst, int(p->roundTripTime));
    return worst;
}

void Session::submit_input(std::uint32_t frame, std::uint16_t keys, std::uint32_t my_frame) {
    Impl& d = *d_;
    if (d.state != State::Running) return;
    d.set_input(d.slot, frame, keys);
    Writer w;
    w.u8(kInput);
    w.u8(std::uint8_t(d.slot));
    w.u32(frame);
    w.u16(keys);
    w.u32(my_frame);
    if (d.cfg.host) d.broadcast_except(w, -1);
    else send(d.server, w);
}

bool Session::ready(std::uint32_t frame) const {
    const Impl& d = *d_;
    for (int s = 0; s < d.players; ++s) {
        const auto& v = d.inputs[std::size_t(s)];
        if (v.size() <= frame || v[frame] < 0) return false;
    }
    return true;
}

std::uint16_t Session::input(int slot, std::uint32_t frame) const {
    const auto& v = d_->inputs[std::size_t(std::clamp(slot, 0, kMaxPlayers - 1))];
    return v.size() > frame && v[frame] >= 0 ? std::uint16_t(v[frame]) : 0;
}

double Session::frames_ahead(std::uint32_t my_frame, double frame_seconds) const {
    const Impl& d = *d_;
    const auto now = Clock::now();
    double ahead = 0;
    bool any = false;
    for (int s = 0; s < d.players; ++s) {
        if (s == d.slot || !d.their_frame[std::size_t(s)]) continue;
        double since = std::chrono::duration<double>(now - d.their_frame_at[std::size_t(s)]).count();
        double one_way = d.rtt_ms[std::size_t(s)] * 0.0005;
        double theirs = double(*d.their_frame[std::size_t(s)]) + (since + one_way) / frame_seconds;
        double a = double(my_frame) - theirs;
        ahead = any ? std::max(ahead, a) : a;
        any = true;
    }
    return any ? ahead : 0;
}

void Session::submit_hash(std::uint32_t frame, std::uint64_t hash) {
    Impl& d = *d_;
    if (d.state != State::Running) return;
    d.set_hash(d.slot, frame, hash);
    Writer w;
    w.u8(kHash);
    w.u8(std::uint8_t(d.slot));
    w.u32(frame);
    w.u64(hash);
    if (d.cfg.host) d.broadcast_except(w, -1);
    else send(d.server, w);
}

long long Session::desync_frame() const { return d_->desync; }

}  // namespace kamo::net
