#pragma once

#include <concepts>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>

namespace shardgate {

template <typename Message>
class Mailbox {
public:
    void post(Message msg) {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return;
            queue_.push(std::move(msg));
        }
        cv_.notify_one();
    }

    std::optional<Message> take() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        Message msg = std::move(queue_.front());
        queue_.pop();
        return msg;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<Message> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
};

template <typename T, typename Message>
concept ISink = requires(T x, Message m) { x.post(std::move(m)); };

template <typename T>
concept IRunnable = requires(T x) {
    x.start();
    x.stop();
};

template <typename T, typename Message>
concept IActor = IRunnable<T> && ISink<T, Message>;

template <typename Derived, typename Message>
class Actor {
public:
    void start() {
        thread_ = std::thread([this] {
            while (auto msg = mailbox_.take())
                static_cast<Derived*>(this)->on_message(std::move(*msg));
        });
    }

    void stop() {
        mailbox_.close();
        if (thread_.joinable()) thread_.join();
    }

    void post(Message msg) { mailbox_.post(std::move(msg)); }

protected:
    ~Actor() = default;

private:
    Mailbox<Message> mailbox_;
    std::thread thread_;
};

}  // namespace shardgate
