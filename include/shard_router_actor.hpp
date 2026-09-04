#pragma once

#include <filesystem>
#include <map>
#include <memory>

#include "actor.hpp"
#include "shard_writer_actor.hpp"

namespace shardgate {

class ShardRouterActor : public Actor<ShardRouterActor, WriteToShard> {
    using Writer = ShardWriterActor<FileShardStore>;
    static_assert(IActor<Writer, WriteToShard>);

public:
    explicit ShardRouterActor(const std::filesystem::path& shard_dir) {
        for (auto protocol :
             {ProtocolType::Https, ProtocolType::Tcp, ProtocolType::Udp,
              ProtocolType::Arp, ProtocolType::Other}) {
            auto writer = std::make_unique<Writer>(
                protocol, FileShardStore(shard_dir, protocol));
            writer->start();
            writers_.emplace(protocol, std::move(writer));
        }
    }

    void on_message(WriteToShard&& write) {
        writers_.at(write.protocol)->post(std::move(write));
    }

    void shutdown() {
        stop();
        for (auto& [proto, writer] : writers_) writer->stop();
    }

private:
    std::map<ProtocolType, std::unique_ptr<Writer>> writers_;
};

static_assert(IActor<ShardRouterActor, WriteToShard>);

}  // namespace shardgate
