#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string_view>
#include <utility>

namespace shardgate::net {

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { reset(); }

    Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

inline bool send_all(int fd, std::string_view data) {
    std::size_t total = 0;
    while (total < data.size()) {
        ssize_t n =
            ::send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        total += static_cast<std::size_t>(n);
    }
    return true;
}

inline ssize_t recv_some(int fd, char* buffer, std::size_t size) {
    for (;;) {
        ssize_t n = ::recv(fd, buffer, size, 0);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

}  // namespace shardgate::net
