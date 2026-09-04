#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "actor.hpp"
#include "sha256.hpp"

namespace shardgate {

struct Authenticate {
    std::string api_key_hash;
    std::function<void(bool)> reply;
};

class AuthActor : public Actor<AuthActor, Authenticate> {
public:
    explicit AuthActor(const std::vector<std::string>& valid_api_keys) {
        for (const auto& key : valid_api_keys)
            valid_hashes_.insert(hash_api_key(key));
    }

    void on_message(Authenticate&& msg) {
        msg.reply(valid_hashes_.contains(msg.api_key_hash));
    }

private:
    std::unordered_set<std::string> valid_hashes_;
};

static_assert(IActor<AuthActor, Authenticate>);

}  // namespace shardgate
