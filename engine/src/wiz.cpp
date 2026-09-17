#include "wiz.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <format>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glaze/glaze.hpp>

namespace wiz {

size_t encodeSetPilot(const Pilot& p, std::span<char> buf) {
    // Turning a bulb off doesn't need to carry channel data — sending it bare
    // is both simpler and matches how the WiZ app itself turns bulbs off.
    std::format_to_n_result<char*> result{buf.data(), 0};
    if (!p.on) {
        result = std::format_to_n(buf.data(), buf.size(),
            R"({{"method":"setPilot","params":{{"state":false}}}})");
    } else if (p.useTemp) {
        result = std::format_to_n(buf.data(), buf.size(),
            R"({{"method":"setPilot","params":{{"state":true,"temp":{},"dimming":{}}}}})",
            p.kelvin, p.dimming);
    } else {
        result = std::format_to_n(buf.data(), buf.size(),
            R"({{"method":"setPilot","params":{{"state":true,"r":{},"g":{},"b":{},"dimming":{}}}}})",
            p.r, p.g, p.b, p.dimming);
    }
    return static_cast<size_t>(result.out - buf.data());
}

Socket::Socket() {
    fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // ephemeral — we're a client, not a bulb
    ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

Socket::~Socket() {
    if (fd_ >= 0) ::close(fd_);
}

void Socket::sendTo(const std::string& ip, uint16_t port, std::string_view payload) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    ::sendto(fd_, payload.data(), payload.size(), 0,
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

namespace {

// Reply shape for the WiZ `registration` discovery handshake. Field names
// follow the commonly documented reverse-engineered protocol (e.g.
// pywizlight); verify against a real bulb reply if discovery misbehaves.
struct RegistrationResult {
    std::string mac;
    bool success = false;
};
struct RegistrationReply {
    std::string method;
    std::optional<RegistrationResult> result;
};

} // namespace

std::vector<Bulb> Socket::discover(std::chrono::milliseconds timeout) {
    std::vector<Bulb> found;

    static constexpr std::string_view kRegistration =
        R"({"method":"registration","params":{"phoneMac":"AAAAAAAAAAAA","register":false,"phoneIp":"0.0.0.0","id":"1"}})";

    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(kPort);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    ::sendto(fd_, kRegistration.data(), kRegistration.size(), 0,
             reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));

    auto deadline = std::chrono::steady_clock::now() + timeout;
    char buf[1024];

    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, static_cast<int>(remaining.count())) <= 0) break;

        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        ssize_t n = ::recvfrom(fd_, buf, sizeof(buf) - 1, 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        buf[n] = '\0';

        RegistrationReply reply{};
        if (glz::read_json(reply, std::string_view(buf, static_cast<size_t>(n)))) continue;
        if (!reply.result || !reply.result->success) continue;

        char ipStr[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));

        bool dup = std::any_of(found.begin(), found.end(), [&](const Bulb& b) {
            return b.mac == reply.result->mac;
        });
        if (!dup) found.push_back(Bulb{ipStr, kPort, reply.result->mac, ""});
    }

    return found;
}

} // namespace wiz
