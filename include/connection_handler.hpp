#pragma once

#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "auth_actor.hpp"
#include "net.hpp"
#include "result.hpp"
#include "shard_router_actor.hpp"
#include "snapshot.hpp"

namespace shardgate {

class Connection {
public:
    explicit Connection(net::Socket socket) : socket_(std::move(socket)) {}

    bool send(const std::string& message) {
        std::lock_guard lock(mutex_);
        if (!socket_.valid()) return false;
        return net::send_all(socket_.fd(), message);
    }

    void close() {
        std::lock_guard lock(mutex_);
        socket_.reset();
    }

    int fd() const { return socket_.fd(); }

private:
    net::Socket socket_;
    std::mutex mutex_;
};

class ConnectionHandler {
public:
    ConnectionHandler(std::shared_ptr<Connection> connection, AuthActor& auth,
                      ShardRouterActor& router)
        : connection_(std::move(connection)), auth_(auth), router_(router) {}

    void run() {
        std::string pending;
        char buffer[4096];

        for (;;) {
            ssize_t n =
                net::recv_some(connection_->fd(), buffer, sizeof(buffer));
            if (n < 0) {
                std::perror("recv");
                break;
            }
            if (n == 0) break;

            pending.append(buffer, static_cast<std::size_t>(n));

            std::size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = trim(pending.substr(0, nl));
                pending.erase(0, nl + 1);
                if (line.empty()) continue;
                if (!handle_line(line)) {
                    connection_->close();
                    return;
                }
            }
        }
        connection_->close();
    }

private:
    static std::string trim(std::string s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    bool handle_line(const std::string& line) {
        return authenticated_ ? on_packet(line) : on_api_key(line);
    }

    bool on_api_key(const std::string& api_key_hash) {
        auto promise = std::make_shared<std::promise<bool>>();
        auto ok = promise->get_future();

        auth_.post(Authenticate{api_key_hash, [promise](bool result) {
                                    promise->set_value(result);
                                }});

        if (!ok.get()) {
            connection_->send(wire::error_document(
                                  {ErrorCode::AuthFailed, "Invalid API Key"}) +
                              "\n");
            return false;
        }

        connection_->send(
            wire::success_document("\"msg\":\"API Key authenticated. You can "
                                   "now send messages.\"") +
            "\n");
        authenticated_ = true;
        return true;
    }

    bool on_packet(const std::string& json_line) {
        ProtocolType protocol =
            to_protocol_type(json::get_string(json_line, "proto"));

        std::weak_ptr<Connection> weak = connection_;
        router_.post(WriteToShard{
            protocol, json_line, [weak](const Result<StoredPacket>& result) {
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
                if (auto conn = weak.lock()) conn->send(doc + "\n");
            }});
        return true;
    }

    std::shared_ptr<Connection> connection_;
    AuthActor& auth_;
    ShardRouterActor& router_;
    bool authenticated_ = false;
};

}  // namespace shardgate
