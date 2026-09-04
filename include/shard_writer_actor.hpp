#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "actor.hpp"
#include "result.hpp"
#include "snapshot.hpp"

namespace shardgate {

struct StoredPacket {
    std::string shard;
    std::string packet_id;
    std::string transaction_id;
};

struct WriteToShard {
    ProtocolType protocol;
    std::string json;
    std::function<void(const Result<StoredPacket>&)> reply;
};

template <typename T>
concept IShardStore = requires(T store, const std::string& doc) {
    { store.insert_one(doc) } -> std::same_as<void>;
};

class FileShardStore {
public:
    FileShardStore(const std::filesystem::path& dir, ProtocolType protocol) {
        std::filesystem::create_directories(dir);
        path_ = dir / (std::string("shard-") + to_string(protocol) + ".jsonl");
    }

    void insert_one(const std::string& document) {
        std::ofstream out(path_, std::ios::app);
        if (!out) throw std::runtime_error("cannot open " + path_.string());
        out << document << '\n';
        if (!out) throw std::runtime_error("write error on " + path_.string());
    }

private:
    std::filesystem::path path_;
};

static_assert(IShardStore<FileShardStore>);

inline std::string new_guid() {
    static std::atomic<std::uint64_t> counter{0};
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%016llx-%08llx",
                  static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(counter.fetch_add(1)));
    return buf;
}

template <IShardStore Store>
class ShardWriterActor : public Actor<ShardWriterActor<Store>, WriteToShard> {
public:
    ShardWriterActor(ProtocolType protocol, Store store)
        : protocol_(protocol), store_(std::move(store)) {}

    void on_message(WriteToShard&& write) { write.reply(handle(write.json)); }

    Result<StoredPacket> handle(const std::string& payload) {
        using R = Result<StoredPacket>;

        auto b = payload.find_first_not_of(" \t");
        if (b == std::string::npos || payload[b] != '{' ||
            payload.back() != '}')
            return R::failure(ErrorCode::MalformedPayload,
                              "snapshot is not a JSON object");

        SnapshotMessage snap = SnapshotMessage::parse(payload);

        if (snap.transaction_id.empty()) snap.transaction_id = new_guid();
        if (snap.client_id.empty()) snap.client_id = snap.source_ip;

        std::string packet_id = new_guid();
        try {
            store_.insert_one(to_document(snap, packet_id));
        } catch (const std::exception& ex) {
            std::cout << "[shard:" << to_string(protocol_)
                      << "] write failed: " << ex.what() << "\n";
            return R::failure(ErrorCode::ShardWriteFailed, ex.what());
        }

        std::cout << "[shard:" << to_string(protocol_) << "] saved "
                  << snap.proto << ' ' << snap.source_ip << " -> "
                  << snap.dest_ip << "\n";
        return R::success({std::string(to_string(protocol_)),
                           std::move(packet_id), snap.transaction_id});
    }

private:
    std::string to_document(const SnapshotMessage& s,
                            const std::string& packet_id) const {
        auto now = std::chrono::system_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        now.time_since_epoch())
                        .count();
        std::ostringstream doc;
        doc << "{" << "\"packetId\":\"" << packet_id << "\","
            << "\"transaction_id\":\"" << s.transaction_id << "\","
            << "\"client_id\":\"" << s.client_id << "\","
            << "\"version\":" << s.version << ","
            << "\"source_port\":" << s.source_port << ","
            << "\"dest_port\":" << s.dest_port << "," << "\"source_ip\":\""
            << s.source_ip << "\"," << "\"dest_ip\":\"" << s.dest_ip << "\","
            << "\"source_mac\":\"" << s.source_mac << "\"," << "\"dest_mac\":\""
            << s.dest_mac << "\"," << "\"proto\":\"" << s.proto << "\","
            << "\"storedAt\":" << secs << "}";
        return doc.str();
    }

    ProtocolType protocol_;
    Store store_;
};

}  // namespace shardgate
