// Assert-based self-checks for the protocol/color/scheduler skeleton.
// No framework: run via ctest or just execute the binary directly.
#include <arpa/inet.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "color.hpp"
#include "effects.hpp"
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

void test_oklab_mix_endpoints_match_inputs() {
    color::Rgb a{1.f, 0.f, 0.f}; // red
    color::Rgb b{0.f, 0.5f, 1.f}; // blue-ish
    color::Rgb at0 = color::mixOklab(a, b, 0.f);
    color::Rgb at1 = color::mixOklab(a, b, 1.f);
    assert(std::fabs(at0.r - a.r) < 0.01f && std::fabs(at0.g - a.g) < 0.01f && std::fabs(at0.b - a.b) < 0.01f);
    assert(std::fabs(at1.r - b.r) < 0.01f && std::fabs(at1.g - b.g) < 0.01f && std::fabs(at1.b - b.b) < 0.01f);
}

void test_effects_parse_roundtrip() {
    assert(effects::parse("aurora") == effects::Id::Aurora);
    assert(effects::name(effects::Id::Aurora) == "aurora");
    assert(effects::parse("not-a-real-effect") == effects::Id::None);
}

void test_rainbow_varies_across_room_position() {
    effects::BulbEffectState state;
    color::Rgb left = effects::evaluate(effects::Id::Rainbow, 0.0, 0.f, state);
    color::Rgb right = effects::evaluate(effects::Id::Rainbow, 0.0, 0.5f, state);
    // Same instant, different position along the room -> different hue.
    assert(color::rgbToHsv(left).h != color::rgbToHsv(right).h);
}

void test_aurora_stays_in_green_violet_band() {
    effects::BulbEffectState state;
    for (double t = 0.0; t < 20.0; t += 1.3) {
        for (float pos : {0.f, 0.3f, 0.7f, 1.f}) {
            color::Rgb c = effects::evaluate(effects::Id::Aurora, t, pos, state);
            color::Hsv hsv = color::rgbToHsv(c);
            assert(hsv.h >= 55.f && hsv.h <= 245.f); // never strays into red/magenta
        }
    }
}

void test_storm_ambient_is_dim_between_strikes() {
    effects::BulbEffectState state;
    state.nextStrikeAt = 1000.0; // push the first strike far into the future
    color::Rgb c = effects::evaluate(effects::Id::Storm, 0.0, 0.f, state);
    assert(color::rgbToHsv(c).v < 0.2f); // dark and stormy, not lit up
}

void test_storm_strike_decays_to_ambient() {
    effects::BulbEffectState state;
    state.nextStrikeAt = 0.0; // force an immediate strike
    color::Rgb flash = effects::evaluate(effects::Id::Storm, 0.0, 0.f, state);
    assert(color::rgbToHsv(flash).v > 0.9f); // the strike itself is a bright flash
    float lastValue = color::rgbToHsv(flash).v;
    for (int i = 0; i < 20; ++i) {
        color::Rgb c = effects::evaluate(effects::Id::Storm, 0.001, 0.f, state);
        float v = color::rgbToHsv(c).v;
        assert(v <= lastValue + 1e-4f); // strictly decaying, never re-brightens on its own
        lastValue = v;
    }
    assert(lastValue < 0.2f); // settled back to ambient
}

void test_strobe_is_capped_and_alternates() {
    // Sampled across a full cycle at 2Hz, strobe must actually turn off at
    // some point — a stuck-on "strobe" would defeat the photosensitivity cap.
    bool sawOff = false, sawOn = false;
    effects::BulbEffectState state; // unused by strobe, but evaluate() needs one
    for (double t = 0.0; t < 0.5; t += 0.05) {
        color::Rgb c = effects::evaluate(effects::Id::Strobe, t, 0.f, state);
        if (color::rgbToHsv(c).v < 0.05f) sawOff = true;
        else sawOn = true;
    }
    assert(sawOff && sawOn);
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
    test_oklab_mix_endpoints_match_inputs();
    test_effects_parse_roundtrip();
    test_rainbow_varies_across_room_position();
    test_aurora_stays_in_green_violet_band();
    test_storm_ambient_is_dim_between_strikes();
    test_storm_strike_decays_to_ambient();
    test_strobe_is_capped_and_alternates();
    std::printf("all tests passed\n");
    return 0;
}
