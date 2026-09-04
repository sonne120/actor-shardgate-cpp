#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "result.hpp"
#include "shardgate_client.hpp"

namespace {

using namespace shardgate;

std::vector<std::string> demo_packets(int count) {
    static const char* protos[] = {"HTTPS", "TCP", "UDP", "ARP", "ICMP"};
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        std::ostringstream json;
        json << "{\"transaction_id\":\"\",\"client_id\":\"\","
             << "\"version\":" << (i + 1) << ','
             << "\"source_port\":" << (40000 + i) << ','
             << "\"dest_port\":" << (i % 2 == 0 ? 443 : 53) << ','
             << "\"source_ip\":\"10.0.0." << (i + 1) << "\","
             << "\"dest_ip\":\"192.168.1." << (i + 1) << "\","
             << "\"source_mac\":\"aa:bb:cc:00:00:0" << (i % 10) << "\","
             << "\"dest_mac\":\"dd:ee:ff:00:00:0" << (i % 10) << "\","
             << "\"proto\":\"" << protos[i % 5] << "\"}";
        out.push_back(json.str());
    }
    return out;
}

int report_fatal(const Error& e) {
    std::cerr << "[" << mongo_code_name(e.code) << "] " << e.message << '\n';
    return is_transport_failure(e) ? 2 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8000;
    std::string key = "valid_api_key_1";
    int demo = 0;
    std::string file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : "";
        };
        if (arg == "--host")
            host = next();
        else if (arg == "--port")
            port = std::atoi(next());
        else if (arg == "--key")
            key = next();
        else if (arg == "--demo")
            demo = std::atoi(next());
        else if (arg == "--help") {
            std::cout << "usage: packet_client [--host H] [--port P]"
                         " [--key API_KEY] [--demo N | file.jsonl]\n";
            return 0;
        } else
            file = arg;
    }

    std::vector<std::string> packets;
    if (demo > 0) {
        packets = demo_packets(demo);
    } else {
        std::ifstream f;
        std::istream* in = &std::cin;
        if (!file.empty()) {
            f.open(file);
            if (!f) {
                std::cerr << "cannot open " << file << '\n';
                return 1;
            }
            in = &f;
        }
        std::string line;
        while (std::getline(*in, line))
            if (!line.empty()) packets.push_back(line);
    }
    if (packets.empty()) {
        std::cerr << "nothing to send (use --demo N, a file, or stdin)\n";
        return 1;
    }

    MasternodeClient client(host, port);

    auto session = client.connect().and_then(
        [&](const Unit&) { return client.authenticate(key); });

    int rc = session.match(
        [&](const Unit&) {
            std::cout << "authenticated against " << host << ':' << port
                      << '\n';
            return 0;
        },
        report_fatal);
    if (rc != 0) return rc;

    int ok = 0, rejected = 0, unreachable = 0;
    for (const auto& packet : packets) {
        client.send_packet(packet).match(
            [&](const StoredPacket& p) {
                ++ok;
                std::cout << "ok        shard=" << p.shard
                          << " packetId=" << p.packet_id
                          << " txn=" << p.transaction_id << '\n';
                return 0;
            },
            [&](const Error& e) {
                if (is_transport_failure(e)) {
                    ++unreachable;
                    std::cout << "unreachable [" << mongo_code_name(e.code)
                              << "] " << e.message << '\n';
                } else {
                    ++rejected;
                    std::cout << "rejected  [" << mongo_code_name(e.code)
                              << " code=" << mongo_code(e.code) << "] "
                              << e.message << '\n';
                }
                return 1;
            });
    }

    client.close();
    std::cout << "---\nsent=" << packets.size() << " ok=" << ok
              << " rejected=" << rejected << " unreachable=" << unreachable
              << '\n';
    return unreachable > 0 ? 2 : 0;
}
