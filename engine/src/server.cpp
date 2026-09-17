#include "server.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <openssl/evp.h>

namespace wiz {

namespace {

std::string base64(const unsigned char* data, size_t len) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += kTable[(n >> 6) & 0x3F];
        out += kTable[n & 0x3F];
    }
    if (len - i == 1) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += "==";
    } else if (len - i == 2) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out += kTable[(n >> 18) & 0x3F];
        out += kTable[(n >> 12) & 0x3F];
        out += kTable[(n >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

// RFC6455 handshake: Sec-WebSocket-Accept = base64(sha1(key + magic GUID)).
std::string acceptKeyFor(const std::string& clientKey) {
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = clientKey + std::string(kGuid);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
    EVP_DigestUpdate(ctx, combined.data(), combined.size());
    EVP_DigestFinal_ex(ctx, digest, &digestLen);
    EVP_MD_CTX_free(ctx);
    return base64(digest, digestLen);
}

std::string findHeader(std::string_view request, std::string_view name) {
    size_t pos = request.find(name);
    if (pos == std::string_view::npos) return {};
    pos += name.size();
    size_t end = request.find("\r\n", pos);
    if (end == std::string_view::npos) return {};
    std::string_view value = request.substr(pos, end - pos);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    return std::string(value);
}

void appendFrameHeader(std::vector<uint8_t>& out, uint8_t opcode, size_t len) {
    out.push_back(static_cast<uint8_t>(0x80 | opcode)); // FIN, server frames are never fragmented
    if (len < 126) {
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(126);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        out.push_back(127);
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<uint8_t>((len >> shift) & 0xFF));
    }
}

std::vector<uint8_t> makeFrame(uint8_t opcode, const void* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.reserve(len + 10);
    appendFrameHeader(frame, opcode, len);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    frame.insert(frame.end(), bytes, bytes + len);
    return frame;
}

// Parses one WS frame out of `buf`. Returns bytes consumed, or 0 if the
// buffer doesn't yet hold a complete frame (wait for more data).
size_t tryParseFrame(const std::vector<uint8_t>& buf, uint8_t& opcode, std::vector<uint8_t>& payloadOut) {
    if (buf.size() < 2) return 0;
    uint8_t b0 = buf[0], b1 = buf[1];
    opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t pos = 2;

    if (len == 126) {
        if (buf.size() < 4) return 0;
        len = (static_cast<uint64_t>(buf[2]) << 8) | buf[3];
        pos = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return 0;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | buf[2 + i];
        pos = 10;
    }

    uint8_t maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (buf.size() < pos + 4) return 0;
        std::memcpy(maskKey, &buf[pos], 4);
        pos += 4;
    }
    if (buf.size() < pos + len) return 0;

    payloadOut.assign(buf.begin() + static_cast<long>(pos), buf.begin() + static_cast<long>(pos + len));
    if (masked)
        for (size_t i = 0; i < payloadOut.size(); ++i) payloadOut[i] ^= maskKey[i % 4];
    return pos + len;
}

} // namespace

struct WsServer::Impl {
    std::string bindAddr;
    uint16_t port;
    int listenFd = -1;
    int epollFd = -1;
    int wakeFd = -1;
    int tickFd = -1;
    std::atomic<bool> running{false};
    MessageHandler onMessage;
    TickHandler onTick;
    std::function<void()> onConnect;
    int tickHz = 0; // armed once tickFd exists, in run()

    struct Client {
        bool handshakeDone = false;
        std::string handshakeBuf;
        std::vector<uint8_t> recvBuf;
        std::vector<uint8_t> outBuf;
        size_t writeOffset = 0;
        bool wantsEpollOut = false;
    };
    std::unordered_map<int, Client> clients;

    std::mutex outgoingMutex; // guards pendingOut, written from other threads
    std::vector<std::vector<uint8_t>> pendingOut;

    void wake() {
        uint64_t one = 1;
        ssize_t written = ::write(wakeFd, &one, sizeof(one));
        (void)written; // EAGAIN just means a wake is already pending — fine
    }

    void epollMod(int fd, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        ::epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
    }

    // Appends raw bytes to a client's outgoing buffer and drains what it can
    // without blocking. Registers for EPOLLOUT if bytes remain queued.
    void queueRaw(int fd, Client& c, const uint8_t* data, size_t len) {
        c.outBuf.insert(c.outBuf.end(), data, data + len);
        flush(fd, c);
    }

    void flush(int fd, Client& c) {
        while (c.writeOffset < c.outBuf.size()) {
            ssize_t n = ::send(fd, c.outBuf.data() + c.writeOffset, c.outBuf.size() - c.writeOffset, MSG_NOSIGNAL);
            if (n > 0) {
                c.writeOffset += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            // Real error or peer gone — the caller's epoll loop will clean this
            // client up on the next EPOLLHUP/EPOLLERR; stop trying to write.
            return;
        }
        bool fullyFlushed = c.writeOffset >= c.outBuf.size();
        if (fullyFlushed) {
            c.outBuf.clear();
            c.writeOffset = 0;
        }
        if (fullyFlushed && c.wantsEpollOut) {
            epollMod(fd, EPOLLIN);
            c.wantsEpollOut = false;
        } else if (!fullyFlushed && !c.wantsEpollOut) {
            epollMod(fd, EPOLLIN | EPOLLOUT);
            c.wantsEpollOut = true;
        }
    }

    void closeClient(int fd) {
        ::epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        clients.erase(fd);
    }

    void handleHandshake(int fd, Client& c) {
        size_t headerEnd = c.handshakeBuf.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return; // wait for more data

        std::string_view request(c.handshakeBuf);
        std::string key = findHeader(request, "Sec-WebSocket-Key:");
        if (key.empty()) {
            closeClient(fd);
            return;
        }

        std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: " + acceptKeyFor(key) + "\r\n\r\n";
        c.handshakeDone = true;

        // Anything the client sent right after the HTTP headers (it shouldn't,
        // but be defensive) belongs to the WS frame stream, not the handshake.
        size_t bodyStart = headerEnd + 4;
        if (bodyStart < c.handshakeBuf.size())
            c.recvBuf.assign(c.handshakeBuf.begin() + static_cast<long>(bodyStart), c.handshakeBuf.end());
        c.handshakeBuf.clear();

        queueRaw(fd, c, reinterpret_cast<const uint8_t*>(response.data()), response.size());
        if (onConnect) onConnect();
    }

    void handleWsData(int fd, Client& c) {
        while (true) {
            uint8_t opcode = 0;
            std::vector<uint8_t> payload;
            size_t consumed = tryParseFrame(c.recvBuf, opcode, payload);
            if (consumed == 0) break;
            c.recvBuf.erase(c.recvBuf.begin(), c.recvBuf.begin() + static_cast<long>(consumed));

            switch (opcode) {
                case 0x1: // text
                    if (onMessage)
                        onMessage(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
                    break;
                case 0x9: { // ping -> pong
                    auto pong = makeFrame(0xA, payload.data(), payload.size());
                    queueRaw(fd, c, pong.data(), pong.size());
                    break;
                }
                case 0x8: // close
                    closeClient(fd);
                    return;
                default:
                    break; // binary/continuation frames from a client: nothing to do with them here
            }
        }
    }
};

WsServer::WsServer(std::string bindAddr, uint16_t port) : impl_(new Impl{}) {
    impl_->bindAddr = std::move(bindAddr);
    impl_->port = port;
}

WsServer::~WsServer() {
    stop();
    delete impl_;
}

void WsServer::onMessage(MessageHandler handler) { impl_->onMessage = std::move(handler); }
void WsServer::onConnect(std::function<void()> handler) { impl_->onConnect = std::move(handler); }
void WsServer::onTick(TickHandler handler, int hz) {
    impl_->onTick = std::move(handler);
    impl_->tickHz = hz; // armed in run(), once tickFd actually exists
}

void WsServer::broadcastText(std::string json) {
    auto frame = makeFrame(0x1, json.data(), json.size());
    std::scoped_lock lock(impl_->outgoingMutex);
    impl_->pendingOut.push_back(std::move(frame));
    impl_->wake();
}

void WsServer::broadcastBinary(std::vector<uint8_t> data) {
    auto frame = makeFrame(0x2, data.data(), data.size());
    std::scoped_lock lock(impl_->outgoingMutex);
    impl_->pendingOut.push_back(std::move(frame));
    impl_->wake();
}

void WsServer::stop() { impl_->running = false; }

void WsServer::run() {
    Impl& s = *impl_;

    s.listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int yes = 1;
    ::setsockopt(s.listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s.port);
    ::inet_pton(AF_INET, s.bindAddr.c_str(), &addr.sin_addr);
    if (::bind(s.listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("wizd: bind");
        return;
    }
    ::listen(s.listenFd, 16);

    s.epollFd = ::epoll_create1(0);
    s.wakeFd = ::eventfd(0, EFD_NONBLOCK);
    s.tickFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (s.tickHz > 0) {
        itimerspec spec{};
        long ns = 1'000'000'000L / s.tickHz;
        spec.it_value.tv_nsec = ns;
        spec.it_interval.tv_nsec = ns;
        ::timerfd_settime(s.tickFd, 0, &spec, nullptr);
    }

    auto addFd = [&](int fd, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        ::epoll_ctl(s.epollFd, EPOLL_CTL_ADD, fd, &ev);
    };
    addFd(s.listenFd, EPOLLIN);
    addFd(s.wakeFd, EPOLLIN);
    addFd(s.tickFd, EPOLLIN);

    s.running = true;
    epoll_event events[64];
    while (s.running) {
        int n = ::epoll_wait(s.epollFd, events, 64, 500);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == s.listenFd) {
                while (true) {
                    int clientFd = ::accept4(s.listenFd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (clientFd < 0) break;
                    int nodelay = 1;
                    ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                    addFd(clientFd, EPOLLIN);
                    s.clients[clientFd] = Impl::Client{};
                }
                continue;
            }

            if (fd == s.wakeFd) {
                uint64_t junk;
                ssize_t r = ::read(s.wakeFd, &junk, sizeof(junk));
                (void)r;
                std::vector<std::vector<uint8_t>> drained;
                {
                    std::scoped_lock lock(s.outgoingMutex);
                    drained.swap(s.pendingOut);
                }
                for (auto& [cfd, client] : s.clients)
                    if (client.handshakeDone)
                        for (auto& frame : drained) s.queueRaw(cfd, client, frame.data(), frame.size());
                continue;
            }

            if (fd == s.tickFd) {
                uint64_t expirations;
                ssize_t r = ::read(s.tickFd, &expirations, sizeof(expirations));
                (void)r;
                if (s.onTick) s.onTick();
                continue;
            }

            auto it = s.clients.find(fd);
            if (it == s.clients.end()) continue;
            Impl::Client& client = it->second;

            bool hangUp = (events[i].events & (EPOLLHUP | EPOLLERR)) != 0;
            if (hangUp) {
                s.closeClient(fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                uint8_t buf[4096];
                ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
                if (r <= 0) {
                    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // spurious wakeup, nothing to do
                    } else {
                        s.closeClient(fd);
                        continue;
                    }
                } else {
                    if (!client.handshakeDone) {
                        client.handshakeBuf.append(reinterpret_cast<char*>(buf), static_cast<size_t>(r));
                        s.handleHandshake(fd, client);
                    } else {
                        client.recvBuf.insert(client.recvBuf.end(), buf, buf + r);
                        s.handleWsData(fd, client);
                    }
                }
            }
            // Re-check: handleHandshake/handleWsData or closeClient may have erased this entry.
            auto again = s.clients.find(fd);
            if (again == s.clients.end()) continue;

            if (events[i].events & EPOLLOUT) s.flush(fd, again->second);
        }
    }

    for (auto& [fd, client] : s.clients) ::close(fd);
    s.clients.clear();
    ::close(s.tickFd);
    ::close(s.wakeFd);
    ::close(s.epollFd);
    ::close(s.listenFd);
}

} // namespace wiz
