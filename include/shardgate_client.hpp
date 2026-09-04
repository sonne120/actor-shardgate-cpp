#pragma once

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "net.hpp"
#include "result.hpp"
#include "sha256.hpp"
#include "shard_writer_actor.hpp"

namespace shardgate {

class MasternodeClient {
public:
    MasternodeClient(std::string host, int port)
        : host_(std::move(host)), port_(port) {}

    Result<Unit> connect() {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* list = nullptr;
        int rc = ::getaddrinfo(host_.c_str(), std::to_string(port_).c_str(),
                               &hints, &list);
        if (rc != 0)
            return Result<Unit>::failure(
                ErrorCode::Unreachable,
                "resolve '" + host_ + "': " + ::gai_strerror(rc));

        Error last{ErrorCode::Unreachable, "no addresses for " + host_};
        for (addrinfo* ai = list; ai != nullptr; ai = ai->ai_next) {
            net::Socket sock{
                ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)};
            if (!sock.valid()) {
                last = {ErrorCode::Unreachable,
                        std::string("socket: ") + std::strerror(errno)};
                continue;
            }
            if (::connect(sock.fd(), ai->ai_addr, ai->ai_addrlen) == 0) {
                socket_ = std::move(sock);
                ::freeaddrinfo(list);
                return Result<Unit>::success({});
            }
            last = {ErrorCode::Unreachable,
                    std::string("connect: ") + std::strerror(errno)};
        }
        ::freeaddrinfo(list);
        return Result<Unit>::failure(std::move(last));
    }

    Result<Unit> authenticate(const std::string& api_key) {
        return request(hash_api_key(api_key))
            .and_then([](const std::string& reply) {
                return wire::parse_reply<Unit>(
                    reply, [](std::string_view) { return Unit{}; });
            });
    }

    Result<StoredPacket> send_packet(const std::string& json_line) {
        return request(json_line).and_then([](const std::string& reply) {
            return wire::parse_reply<StoredPacket>(
                reply, [](std::string_view doc) {
                    return StoredPacket{
                        json::get_string(doc, "shard"),
                        json::get_string(doc, "packetId"),
                        json::get_string(doc, "transaction_id")};
                });
        });
    }

    void close() { socket_.reset(); }

private:
    Result<std::string> request(const std::string& line) {
        if (!socket_.valid())
            return Result<std::string>::failure(ErrorCode::Unreachable,
                                                "not connected");
        if (!net::send_all(socket_.fd(), line + "\n"))
            return Result<std::string>::failure(
                ErrorCode::Unreachable,
                std::string("send: ") + std::strerror(errno));
        return read_line();
    }

    Result<std::string> read_line() {
        char chunk[4096];
        for (;;) {
            auto nl = pending_.find('\n');
            if (nl != std::string::npos) {
                std::string line = pending_.substr(0, nl);
                pending_.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return Result<std::string>::success(std::move(line));
            }

            ssize_t n = net::recv_some(socket_.fd(), chunk, sizeof(chunk));
            if (n == 0)
                return Result<std::string>::failure(
                    ErrorCode::Unreachable, "connection closed by ShardGate");
            if (n < 0)
                return Result<std::string>::failure(
                    ErrorCode::Unreachable,
                    std::string("recv: ") + std::strerror(errno));
            pending_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    std::string host_;
    int port_;
    net::Socket socket_;
    std::string pending_;
};

}  // namespace shardgate
