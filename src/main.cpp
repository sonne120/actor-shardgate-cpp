#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "auth_actor.hpp"
#include "main_server.hpp"
#include "shard_router_actor.hpp"

namespace {

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

}  // namespace

int main() {
    using namespace shardgate;

    int port = std::stoi(env_or("SHARDGATE_PORT", "8000"));
    auto keys =
        split_csv(env_or("SHARDGATE_KEYS", "valid_api_key_1,valid_api_key_2"));
    std::string shard_dir = env_or("SHARDGATE_SHARDS", "data");

    AuthActor auth(keys);
    auth.start();

    ShardRouterActor router(shard_dir);
    router.start();

    std::cout << "ShardGate starting on TCP port " << port
              << " with 5 shard nodes" << "\n";

    MainServer server(auth, router, port);
    int rc = server.run();

    router.shutdown();
    auth.stop();
    return rc;
}
