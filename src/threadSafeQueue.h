#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class ThreadSafeQueue {
public:
    void push(const T& value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(value);
        }
        cond_empty_.notify_one();
    }
    void push(T&& value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cond_empty_.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }
    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_empty_;
};