#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include "util.h"

// 自动收缩的线程安全阻塞队列
template <typename T>
class AutoShrinkBlockingQueue {
public:
    // shrink_check_interval: 每多少次pop/try_pop检测一次是否需要shrink
    // shrink_factor: 收缩判定阈值（当前队长低于 high mark 的 shrink_factor 比例时触发shrink，推荐 0.15~0.25）
    explicit AutoShrinkBlockingQueue(
        size_t shrink_check_interval = 150,
        float shrink_factor = 0.25f)
        : shrink_check_interval_(shrink_check_interval),
          shrink_factor_(shrink_factor),
          op_count_(0),
          last_high_mark_(0)
    {}

    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push_back(value);
        if (queue_.size() > last_high_mark_)
            last_high_mark_ = queue_.size();
        lock.unlock();
        cond_empty_.notify_one();
    }

    void push(T&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push_back(std::move(value));
        if (queue_.size() > last_high_mark_)
            last_high_mark_ = queue_.size();
        lock.unlock();
        cond_empty_.notify_one();
    }

    // 阻塞直到有数据
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_empty_.wait(lock, [this]{ return !queue_.empty(); });
        T val = std::move(queue_.front());
        queue_.pop_front();
        auto_shrink();
        return val;
    }

    // 非阻塞出队
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T val = std::move(queue_.front());
        queue_.pop_front();
        auto_shrink();
        return val;
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 辅助：可选导出高水位值
    size_t last_high_mark() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return last_high_mark_;
    }

private:
    void auto_shrink() {
        // 这里假设已持有mutex_
        ++op_count_;
        // 每 shrink_check_interval 次pop后检测一次
        if (op_count_ >= shrink_check_interval_) {
            op_count_ = 0;
            // === 优化: 空队列也允许 shrink，这样内存和 high_mark 也能归零，行为语义更自然 ===
            if (queue_.empty() || queue_.size() < last_high_mark_ * shrink_factor_) {
                ////test
                // {
                //     SPDLOG_INFO("[Before auto_shrink], queue size:{}, old_last_high_mark:{}, shrink_factor_:{}, last_high_mark_ * shrink_factor_ = {}", 
                //         queue_.size(), last_high_mark_, shrink_factor_, last_high_mark_ * shrink_factor_);
                //     LogMemorySnapshot("[Before auto_shrink]");
                // }
                {
                    // 用move迭代器高效转移（支持move-only类型，无拷贝)
                    // 如果元素类型不可 move，可fallback到常规 copy 构造法
                    std::deque<T> newq(
                        std::make_move_iterator(queue_.begin()), 
                        std::make_move_iterator(queue_.end())
                    );
                    queue_.swap(newq);
                    last_high_mark_ = queue_.size(); // 空时会归零
                }
                // {
                //     SPDLOG_INFO("[After auto_shrink], queue size:{}, new_last_high_mark:{}, shrink_factor_:{}, last_high_mark_ * shrink_factor_ = {}", 
                //         queue_.size(), last_high_mark_, shrink_factor_, last_high_mark_ * shrink_factor_);
                //     LogMemorySnapshot("[After auto_shrink]");
                // }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cond_empty_;
    std::deque<T> queue_;
    // shrink参数及内部高水位
    size_t shrink_check_interval_;
    float shrink_factor_;
    size_t op_count_;
    size_t last_high_mark_ = 0; // 跟踪历史峰值，做相对缩容判定
};