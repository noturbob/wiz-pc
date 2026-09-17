#pragma once
// Minimal single-purpose WebSocket server (RFC6455) for the dashboard.
// No TLS, no HTTP routing beyond the upgrade handshake — this only ever runs
// on localhost or a LAN you already trust (see --lan in main.cpp).
//
// Not uWebSockets: that library ships no CMake build (Makefile-only
// upstream), which makes it fragile to vendor via FetchContent. A single
// trusted local client doesn't need its throughput; a compact epoll server
// gives the same non-blocking model with far less integration risk.
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wiz {

class WsServer {
public:
    using MessageHandler = std::function<void(std::string_view json)>;
    using TickHandler = std::function<void()>;

    WsServer(std::string bindAddr, uint16_t port);
    ~WsServer();
    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    // Called on the server's own thread for every text frame a client sends.
    void onMessage(MessageHandler handler);

    // Called on the server's own thread right after a client's handshake
    // completes — the moment to (re-)broadcast current state so a freshly
    // connected dashboard doesn't have to wait for something to change.
    void onConnect(std::function<void()> handler);

    // Called on the server's own thread at `hz`, driven by a timerfd in the
    // same epoll loop — this doubles as the engine tick for now (single
    // thread is correct until effects/audio/screen add real producers).
    void onTick(TickHandler handler, int hz);

    // Runs the accept/IO loop. Blocks; call from main() or a dedicated thread.
    void run();
    void stop();

    // Thread-safe: queues a frame to every connected client.
    void broadcastText(std::string json);
    void broadcastBinary(std::vector<uint8_t> data);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace wiz
