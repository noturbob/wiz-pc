// Assert-based self-checks for the protocol/color/scheduler skeleton.
// No framework: run via ctest or just execute the binary directly.
#include <arpa/inet.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "color.hpp"
#include "scheduler.hpp"
#include "wiz.hpp"

using namespace wiz;

namespace {

void test_encode_off() {
    Pilot p;
    p.on = false;
    std::array<char, 128> buf;
    size_t n = encodeSetPilot(p, buf);
    std::string s(buf.data(), n);
    assert(s.find("\"state\":false") != std::string::npos);
    assert(s.find("\"r\"") == std::string::npos); // off payload carries no channel data
}

void test_encode_rgb() {
    Pilot p;
    p.on = true;
    p.useTemp = false;
    p.r = 255;
    p.g = 10;
    p.b = 0;
    p.dimming = 80;
    std::array<char, 128> buf;
    size_t n = encodeSetPilot(p, buf);
    std::string s(buf.data(), n);
    assert(s.find("\"r\":255") != std::string::npos);
    assert(s.find("\"dimming\":80") != std::string::npos);
}

void test_encode_temp() {
    Pilot p;
    p.on = true;
    p.useTemp = true;
    p.kelvin = 2700;
    p.dimming = 50;
    std::array<char, 128> buf;
    size_t n = encodeSetPilot(p, buf);
    std::string s(buf.data(), n);
    assert(s.find("\"temp\":2700") != std::string::npos);
}

void test_color_low_saturation_uses_temp() {
    // Near-white RGB should fall back to the temp channel, not raw RGB.
    auto p = color::toPilot({0.95f, 0.93f, 0.9f}, 0.7f);
    assert(p.on);
    assert(p.useTemp);
}

void test_color_saturated_uses_rgb() {
    auto p = color::toPilot({1.f, 0.f, 0.f}, 0.7f);
    assert(p.on);
    assert(!p.useTemp);
    assert(p.r > p.g);
}

void test_color_zero_brightness_is_off() {
    auto p = color::toPilot({1.f, 0.f, 0.f}, 0.f);
    assert(!p.on);
}

void test_scheduler_latest_wins_and_rate_limits() {
    constexpr uint16_t kTestPort = 40199;

    // Bind a raw loopback socket so we can see what the scheduler actually sends.
    int rx = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kTestPort);
    int bound = ::bind(rx, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(bound == 0);
    timeval tv{0, 200000}; // 200ms
    ::setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    Socket sock;
    Scheduler sched(sock);
    sched.addBulb(Bulb{"127.0.0.1", kTestPort, "test", ""}, /*maxHz=*/20);

    Pilot stale;
    stale.on = true;
    stale.useTemp = false;
    stale.r = 1;
    stale.dimming = 50;
    Pilot fresh;
    fresh.on = true;
    fresh.useTemp = false;
    fresh.r = 254;
    fresh.dimming = 50;

    sched.push(0, stale);
    sched.push(0, fresh); // overwrites — the scheduler must never emit `stale`

    int sent = sched.tick();
    assert(sent == 1); // first tick always sends (no prior send recorded)

    char buf[256];
    ssize_t n = ::recv(rx, buf, sizeof(buf) - 1, 0);
    assert(n > 0);
    std::string s(buf, static_cast<size_t>(n));
    assert(s.find("\"r\":254") != std::string::npos);
    assert(s.find("\"r\":1,") == std::string::npos);

    int sentAgain = sched.tick(); // nothing new pushed, rate window not elapsed
    assert(sentAgain == 0);

    ::close(rx);
}

} // namespace

int main() {
    test_encode_off();
    test_encode_rgb();
    test_encode_temp();
    test_color_low_saturation_uses_temp();
    test_color_saturated_uses_rgb();
    test_color_zero_brightness_is_off();
    test_scheduler_latest_wins_and_rate_limits();
    std::printf("all tests passed\n");
    return 0;
}
