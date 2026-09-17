#pragma once
// Wire-level protocol for Philips WiZ bulbs: UDP JSON on port 38899.
// See color.hpp for the RGB/kelvin decision, scheduler.hpp for send timing.
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wiz {

constexpr uint16_t kPort = 38899; // real bulbs always listen here

// What we want a bulb to display. Either RGB or a colour temperature — a bulb
// can't do both at once, so this is a tagged union in spirit, not two channels.
struct Pilot {
    bool on = true;
    uint8_t r = 0, g = 0, b = 0;    // used when useTemp == false
    uint16_t kelvin = 4000;         // used when useTemp == true (~2200-6500 on most bulbs)
    uint8_t dimming = 100;          // 10-100; WiZ firmware ignores/misbehaves below ~10
    bool useTemp = false;
};

struct Bulb {
    std::string ip;
    uint16_t port = kPort;   // overridden for --fake bulbs, which run on loopback
    std::string mac;
    std::string moduleName;  // filled in later via getSystemConfig; empty until then
};

// Formats a setPilot UDP payload into `buf` and returns the bytes written.
// No heap allocation — this runs on the per-tick hot path.
size_t encodeSetPilot(const Pilot& p, std::span<char> buf);

// Non-blocking UDP endpoint used for both discovery and steady-state control.
class Socket {
public:
    Socket();
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    void sendTo(const std::string& ip, uint16_t port, std::string_view payload);

    // Broadcasts a `registration` message and collects replies for `timeout`.
    // Blocking; call once at startup or from a background thread.
    std::vector<Bulb> discover(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));

private:
    int fd_ = -1;
};

} // namespace wiz
