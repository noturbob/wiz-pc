// wizd: WiZ bulb control daemon.
//
// Runs a WebSocket server for the dashboard (server.cpp) and drives a
// Scheduler of bulbs (real, discovered over UDP, or --fake for hardware-free
// development). Effects (step 3+), audio (step 4) and screen sync (step 5)
// will add their own producer threads later; for now everything runs on the
// WsServer's own event loop, which is correct until there's a second real
// producer of bulb state.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <glaze/glaze.hpp>

#include "color.hpp"
#include "scheduler.hpp"
#include "server.hpp"
#include "wiz.hpp"

namespace {

std::atomic<bool> g_running{true};
wiz::WsServer* g_server = nullptr; // set just before WsServer::run(); see onSignal
void onSignal(int) {
    g_running = false;
    if (g_server) g_server->stop(); // WsServer::run()'s loop is otherwise unbounded
}

// Minimal virtual bulb for hardware-free development: listens on
// 127.0.0.1:<port> and logs whatever setPilot payload it receives.
void runFakeBulb(int index, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    timeval tv{0, 200000}; // 200ms — lets the loop notice shutdown
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[1024];
    while (g_running) {
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            std::printf("[fake bulb %d :%u] %s\n", index, port, buf);
        }
    }
    ::close(fd);
}

// Incoming dashboard command: {"type":"set","targets":[0,2],"r":1,"g":0,"b":0,"brightness":0.8}
// An empty (or absent) targets list means "all bulbs".
struct SetCommand {
    std::string type;
    std::vector<int> targets;
    float r = 1.f, g = 1.f, b = 1.f; // linear 0..1
    float brightness = 1.f;          // 0..1
};

// Live mirror sent to the dashboard at a fixed rate: one 5-byte record per
// bulb, [index, r, g, b, dimming]. The UI reads this in place of re-deriving
// state from JSON, so it always shows exactly what the bulb was told to do.
std::vector<uint8_t> buildLiveFrame(const wiz::Scheduler& sched) {
    std::vector<uint8_t> frame;
    frame.reserve(sched.size() * 5);
    for (size_t i = 0; i < sched.size(); ++i) {
        const wiz::Pilot& p = sched.pending(i);
        wiz::color::Rgb rgb = p.useTemp ? wiz::color::kelvinToRgb(static_cast<float>(p.kelvin))
                                         : wiz::color::Rgb{p.r / 255.f, p.g / 255.f, p.b / 255.f};
        uint8_t dim = p.on ? p.dimming : 0;
        frame.push_back(static_cast<uint8_t>(i));
        frame.push_back(static_cast<uint8_t>(std::lround(std::clamp(rgb.r, 0.f, 1.f) * 255.f)));
        frame.push_back(static_cast<uint8_t>(std::lround(std::clamp(rgb.g, 0.f, 1.f) * 255.f)));
        frame.push_back(static_cast<uint8_t>(std::lround(std::clamp(rgb.b, 0.f, 1.f) * 255.f)));
        frame.push_back(dim);
    }
    return frame;
}

struct BulbInfo { int index; std::string ip; std::string mac; };
struct StateMsg { std::string type = "state"; std::vector<BulbInfo> bulbs; };

std::string buildStateJson(const wiz::Scheduler& sched) {
    StateMsg msg;
    for (size_t i = 0; i < sched.size(); ++i) {
        const wiz::Bulb& b = sched.bulb(i);
        msg.bulbs.push_back({static_cast<int>(i), b.ip, b.mac});
    }
    return glz::write_json(msg).value_or("{}");
}

void serve(wiz::Scheduler& sched, const std::string& bindAddr, uint16_t port) {
    wiz::WsServer server(bindAddr, port);

    server.onMessage([&](std::string_view json) {
        SetCommand cmd{};
        if (glz::read_json(cmd, json)) return; // malformed — ignore
        if (cmd.type != "set") return;

        wiz::Pilot pilot = wiz::color::toPilot({cmd.r, cmd.g, cmd.b}, cmd.brightness);
        if (cmd.targets.empty()) {
            for (size_t i = 0; i < sched.size(); ++i) sched.push(i, pilot);
        } else {
            for (int t : cmd.targets)
                if (t >= 0 && static_cast<size_t>(t) < sched.size()) sched.push(static_cast<size_t>(t), pilot);
        }
    });

    server.onTick(
        [&] {
            sched.tick();
            server.broadcastBinary(buildLiveFrame(sched));
        },
        /*hz=*/30);

    // Every connecting client gets the current bulb list right away, rather
    // than waiting for something to change. Broadcasting (not unicasting) to
    // whoever's connected is fine here: this message is small, rare, and idempotent.
    server.onConnect([&] { server.broadcastText(buildStateJson(sched)); });

    std::printf("wizd: serving ws://%s:%u (%zu bulb(s))\n", bindAddr.c_str(), port, sched.size());
    g_server = &server;
    server.run(); // blocks until a signal calls server.stop()
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    int fakeCount = 0;
    bool discoverOnly = false;
    std::string bindAddr = "127.0.0.1";
    uint16_t port = 7878;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fake") == 0 && i + 1 < argc) fakeCount = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--discover") == 0) discoverOnly = true;
        else if (std::strcmp(argv[i], "--lan") == 0) bindAddr = "0.0.0.0";
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
    }

    if (discoverOnly) {
        wiz::Socket sock;
        std::printf("Discovering WiZ bulbs (1.5s)...\n");
        auto bulbs = sock.discover();
        for (auto& b : bulbs) std::printf("  %s  mac=%s\n", b.ip.c_str(), b.mac.c_str());
        std::printf("found %zu bulb(s)\n", bulbs.size());
        return 0;
    }

    wiz::Socket sock;
    wiz::Scheduler sched(sock);
    std::vector<std::thread> fakeBulbs;

    if (fakeCount > 0) {
        for (int i = 0; i < fakeCount; ++i) {
            fakeBulbs.emplace_back(runFakeBulb, i, static_cast<uint16_t>(40000 + i));
            sched.addBulb(wiz::Bulb{"127.0.0.1", static_cast<uint16_t>(40000 + i), "fake" + std::to_string(i), "fake"});
        }
        std::printf("wizd: %d fake bulb(s) on 127.0.0.1:40000+\n", fakeCount);
    } else {
        std::printf("Discovering WiZ bulbs (1.5s)...\n");
        for (auto& b : sock.discover()) sched.addBulb(b);
        std::printf("wizd: %zu real bulb(s) found\n", sched.size());
    }

    serve(sched, bindAddr, port);

    g_running = false;
    for (auto& th : fakeBulbs) th.join();
    return 0;
}
