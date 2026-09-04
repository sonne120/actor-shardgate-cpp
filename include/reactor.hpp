#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "auth_actor.hpp"
#include "net.hpp"
#include "result.hpp"
#include "shard_router_actor.hpp"
#include "snapshot.hpp"

namespace shardgate {

class Reactor {
public:
    Reactor(AuthActor& auth, ShardRouterActor& router, int port)
        : auth_(auth), router_(router), port_(port) {}

    ~Reactor() {
        if (epfd_ >= 0) ::close(epfd_);
        if (wakeup_ >= 0) ::close(wakeup_);
    }

    int run() {
        net::Socket listener{make_listener()};
        if (!listener.valid()) return 1;

        epfd_ = ::epoll_create1(0);
        wakeup_ = ::eventfd(0, EFD_NONBLOCK);
        if (epfd_ < 0 || wakeup_ < 0) {
            std::perror("epoll/eventfd");
            return 1;
        }
        watch(listener.fd(), EPOLLIN);
        watch(wakeup_, EPOLLIN);

        std::cout << "ShardGate listening on 0.0.0.0:" << port_
                  << " (epoll reactor)\n";

        epoll_event events[64];
        while (running_.load(std::memory_order_relaxed)) {
            int n = ::epoll_wait(epfd_, events, 64, -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::perror("epoll_wait");
                return 1;
            }
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                uint32_t ev = events[i].events;
                if (fd == listener.fd()) {
                    accept_all(listener.fd());
                } else if (fd == wakeup_) {
                    drain_tasks();
                } else {
                    auto it = conns_.find(fd);
                    if (it == conns_.end()) continue;
                    if (ev & (EPOLLERR | EPOLLHUP)) {
                        close_conn(fd);
                        continue;
                    }
                    if (ev & EPOLLIN) {
                        if (!on_readable(it->second)) {
                            close_conn(fd);
                            continue;
                        }
                    }
                    if (ev & EPOLLOUT) on_writable(it->second);
                }
            }
        }
        conns_.clear();
        return 0;
    }

    void stop() {
        running_.store(false, std::memory_order_relaxed);
        wake();
    }

private:
    struct Conn {
        net::Socket socket;
        uint64_t generation = 0;
        std::string in;
        std::string out;
        bool authenticated = false;
        bool close_after_flush = false;
    };

    static void make_nonblocking(int fd) {
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }

    int make_listener() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::perror("socket");
            return -1;
        }
        int yes = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::perror("bind");
            ::close(fd);
            return -1;
        }
        if (::listen(fd, 128) < 0) {
            std::perror("listen");
            ::close(fd);
            return -1;
        }
        make_nonblocking(fd);
        return fd;
    }

    void watch(int fd, uint32_t ev) {
        epoll_event e{};
        e.events = ev;
        e.data.fd = fd;
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &e);
    }

    void rearm(Conn& c) {
        epoll_event e{};
        e.events = EPOLLIN | (c.out.empty() ? 0U : EPOLLOUT);
        e.data.fd = c.socket.fd();
        ::epoll_ctl(epfd_, EPOLL_CTL_MOD, c.socket.fd(), &e);
    }

    void accept_all(int listener_fd) {
        for (;;) {
            int fd = ::accept(listener_fd, nullptr, nullptr);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR) continue;
                std::perror("accept");
                return;
            }
            make_nonblocking(fd);
            Conn conn;
            conn.socket = net::Socket{fd};
            conn.generation = ++next_generation_;
            conns_.emplace(fd, std::move(conn));
            watch(fd, EPOLLIN);
        }
    }

    bool on_readable(Conn& c) {
        char buffer[4096];
        for (;;) {
            ssize_t n = ::recv(c.socket.fd(), buffer, sizeof(buffer), 0);
            if (n > 0) {
                c.in.append(buffer, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return false;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            return false;
        }

        std::size_t nl;
        while ((nl = c.in.find('\n')) != std::string::npos) {
            std::string line = trim(c.in.substr(0, nl));
            c.in.erase(0, nl + 1);
            if (line.empty()) continue;
            if (c.close_after_flush) continue;
            handle_line(c, line);
        }
        return true;
    }

    void on_writable(Conn& c) {
        while (!c.out.empty()) {
            ssize_t n =
                ::send(c.socket.fd(), c.out.data(), c.out.size(), MSG_NOSIGNAL);
            if (n > 0) {
                c.out.erase(0, static_cast<std::size_t>(n));
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n < 0 && errno == EINTR) continue;
            close_conn(c.socket.fd());
            return;
        }
        if (c.out.empty() && c.close_after_flush) {
            close_conn(c.socket.fd());
            return;
        }
        rearm(c);
    }

    void close_conn(int fd) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        conns_.erase(fd);
    }

    void handle_line(Conn& c, const std::string& line) {
        int fd = c.socket.fd();
        uint64_t gen = c.generation;

        if (!c.authenticated) {
            auth_.post(Authenticate{
                line, [this, fd, gen](bool ok) {
                    post_to_loop([this, fd, gen, ok] {
                        Conn* c = find(fd, gen);
                        if (!c) return;
                        if (ok) {
                            c->authenticated = true;
                            queue_reply(*c,
                                        wire::success_document(
                                            "\"msg\":\"API Key authenticated. "
                                            "You can now send messages.\""));
                        } else {
                            c->close_after_flush = true;
                            queue_reply(
                                *c, wire::error_document({ErrorCode::AuthFailed,
                                                          "Invalid API Key"}));
                        }
                    });
                }});
            return;
        }

        ProtocolType protocol =
            to_protocol_type(json::get_string(line, "proto"));
        router_.post(WriteToShard{
            protocol, line,
            [this, fd, gen](const Result<StoredPacket>& result) {
                std::string doc = result.match(
                    [](const StoredPacket& p) {
                        return wire::success_document(
                            "\"n\":1"
                            ",\"shard\":\"" +
                            wire::escape(p.shard) + "\",\"packetId\":\"" +
                            wire::escape(p.packet_id) +
                            "\",\"transaction_id\":\"" +
                            wire::escape(p.transaction_id) + "\"");
                    },
                    [](const Error& e) { return wire::error_document(e); });
                post_to_loop([this, fd, gen, doc = std::move(doc)] {
                    if (Conn* c = find(fd, gen)) queue_reply(*c, doc);
                });
            }});
    }

    void queue_reply(Conn& c, const std::string& doc) {
        c.out += doc;
        c.out += '\n';
        on_writable(c);
    }

    Conn* find(int fd, uint64_t gen) {
        auto it = conns_.find(fd);
        if (it == conns_.end() || it->second.generation != gen) return nullptr;
        return &it->second;
    }

    void post_to_loop(std::function<void()> fn) {
        {
            std::lock_guard lock(tasks_mutex_);
            tasks_.push_back(std::move(fn));
        }
        wake();
    }

    void drain_tasks() {
        uint64_t tmp;
        while (::read(wakeup_, &tmp, sizeof(tmp)) > 0) {
        }
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard lock(tasks_mutex_);
            batch.swap(tasks_);
        }
        for (auto& fn : batch) fn();
    }

    void wake() {
        uint64_t one = 1;
        [[maybe_unused]] ssize_t n = ::write(wakeup_, &one, sizeof(one));
    }

    static std::string trim(std::string s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    AuthActor& auth_;
    ShardRouterActor& router_;
    int port_;
    int epfd_ = -1;
    int wakeup_ = -1;
    uint64_t next_generation_ = 0;
    std::atomic<bool> running_{true};
    std::unordered_map<int, Conn> conns_;
    std::mutex tasks_mutex_;
    std::vector<std::function<void()>> tasks_;
};

}  // namespace shardgate
