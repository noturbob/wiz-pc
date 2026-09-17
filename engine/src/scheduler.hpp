#pragma once
// Per-bulb send scheduler: latest-wins, rate-limited. Effects/UI push a new
// target state as often as they like; tick() only ever sends the most recent
// one, and only when that bulb's rate budget allows it. A slow or unreachable
// bulb can never build up a backlog of stale commands.
#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

#include "wiz.hpp"

namespace wiz {

class Scheduler {
public:
    explicit Scheduler(Socket& sock) : sock_(sock) {}

    void addBulb(Bulb bulb, int maxHz = 20) {
        bulbs_.push_back(std::move(bulb));
        pending_.push_back({});
        dirty_.push_back(false);
        lastSent_.push_back({}); // epoch: far enough in the past to send immediately
        periodMs_.push_back(std::chrono::milliseconds(1000 / std::max(1, maxHz)));
    }

    void push(size_t bulbIdx, const Pilot& p) {
        pending_.at(bulbIdx) = p;
        dirty_.at(bulbIdx) = true;
    }

    // Call from the engine tick (e.g. 50Hz). Returns how many bulbs were sent.
    int tick() {
        auto now = std::chrono::steady_clock::now();
        int sent = 0;
        for (size_t i = 0; i < bulbs_.size(); ++i) {
            if (!dirty_[i]) continue;
            if (now - lastSent_[i] < periodMs_[i]) continue;

            std::array<char, 192> buf;
            size_t n = encodeSetPilot(pending_[i], buf);
            sock_.sendTo(bulbs_[i].ip, bulbs_[i].port, std::string_view(buf.data(), n));

            dirty_[i] = false;
            lastSent_[i] = now;
            ++sent;
        }
        return sent;
    }

    size_t size() const { return bulbs_.size(); }
    const Bulb& bulb(size_t i) const { return bulbs_.at(i); }
    // The state we believe this bulb should be in right now — regardless of
    // whether it's been sent yet. Used to mirror live state to the dashboard.
    const Pilot& pending(size_t i) const { return pending_.at(i); }

private:
    Socket& sock_;
    std::vector<Bulb> bulbs_;
    std::vector<Pilot> pending_;
    std::vector<bool> dirty_;
    std::vector<std::chrono::steady_clock::time_point> lastSent_;
    std::vector<std::chrono::milliseconds> periodMs_;
};

} // namespace wiz
