#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

#include "auth_actor.hpp"
#include "connection_handler.hpp"
#include "shard_router_actor.hpp"

namespace shardgate {

class MainServer {
public:
    MainServer(AuthActor& auth, ShardRouterActor& router, int port)
        : auth_(auth), router_(router), port_(port) {}

    int run() {
        net::Socket server{::socket(AF_INET, SOCK_STREAM, 0)};
        if (!server.valid()) {
            std::perror("socket");
            return 1;
        }

        int yes = 1;
        setsockopt(server.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(static_cast<uint16_t>(port_));

        if (::bind(server.fd(), reinterpret_cast<sockaddr*>(&addr),
                   sizeof(addr)) < 0) {
            std::perror("bind");
            return 1;
        }
        if (::listen(server.fd(), 16) < 0) {
            std::perror("listen");
            return 1;
        }

        std::cout << "ShardGate listening on 0.0.0.0:" << port_ << "\n";

        for (;;) {
            sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);
            int client_fd = ::accept(
                server.fd(), reinterpret_cast<sockaddr*>(&peer), &peer_len);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                std::perror("accept");
                break;
            }

            char ip[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            std::cout << "Connection from " << ip << ':' << ntohs(peer.sin_port)
                      << "\n";

            auto connection =
                std::make_shared<Connection>(net::Socket{client_fd});
            std::thread([connection, this] {
                ConnectionHandler handler(connection, auth_, router_);
                handler.run();
            }).detach();
        }
        return 0;
    }

private:
    AuthActor& auth_;
    ShardRouterActor& router_;
    int port_;
};

}  // namespace shardgate
