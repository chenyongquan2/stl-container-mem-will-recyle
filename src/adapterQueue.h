#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <type_traits>

/**
 * @brief 自动收缩、线程安全的阻塞队列
 * 
 * T 必须可 move 构造和 move 赋值
 * 提供线程安全的 push/pop/try_pop/size/empty，自动按需收缩内存
 * 注意：size/empty 仅为快照，不能用于并发逻辑判断
 */
template <typename T>
class AutoShrinkBlockingQueue {
    static_assert(std::is_move_constructible<T>::value,
                  "AutoShrinkBlockingQueue: T must be move constructible");
    static_assert(std::is_move_assignable<T>::value,
                  "AutoShrinkBlockingQueue: T must be move assignable");

public:
    /**
     * @param shrink_check_interval 每多少次 pop/try_pop 检查一次是否需要 shrink
     * @param shrink_factor 当前队长低于 high mark 的 shrink_factor 时触发 shrink（推荐 0.15~0.25）
     */
    explicit AutoShrinkBlockingQueue(
        size_t shrink_check_interval = 150,
        float shrink_factor = 0.25f
    )
        : shrink_check_interval_(shrink_check_interval),
          shrink_factor_(shrink_factor),
          op_count_(0),
          last_high_mark_(0)
    {}

    AutoShrinkBlockingQueue(const AutoShrinkBlockingQueue&) = delete;
    AutoShrinkBlockingQueue& operator=(const AutoShrinkBlockingQueue&) = delete;

    /**
     * @brief 线程安全入队
     */
    void push(const T& value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push_back(value);
            if (queue_.size() > last_high_mark_) {
                last_high_mark_ = queue_.size();
            }
        }
        cond_empty_.notify_one();
    }
    void push(T&& value) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push_back(std::move(value));
            if (queue_.size() > last_high_mark_) {
                last_high_mark_ = queue_.size();
            }
        }
        cond_empty_.notify_one();
    }

    /**
     * @brief 阻塞直到有数据，线程安全
     * @return 出队元素
     * @note 如果 T 的移动构造/赋值抛异常，队列元素将丢失
     */
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_empty_.wait(lock, [this]{ return !queue_.empty(); });
        T val = std::move(queue_.front());
        queue_.pop_front();
        auto_shrink();
        return val;
    }

    /**
     * @brief 非阻塞尝试出队，线程安全
     * @return 有数据时返回元素，否则返回空
     */
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T val = std::move(queue_.front());
        queue_.pop_front();
        auto_shrink();
        return val;
    }

    /**
     * @brief 队列当前元素数，仅做信息快照，不可用于业务并发逻辑
     */
    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief 队列是否为空，仅做信息快照，不可用于业务并发逻辑
     */
    bool empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief 返回历史最大队列长度，仅作为信息描述
     */
    size_t last_high_mark() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return last_high_mark_;
    }

private:
    // 自动 shrink 原则：每 shrink_check_interval 次 pop 检查一次
    void auto_shrink() {
        ++op_count_;
        if (op_count_ >= shrink_check_interval_) {
            op_count_ = 0;
             // 空队列也允许 shrink，这样内存和 high_mark 也能归零，行为语义更自然 ===
            if (queue_.empty() || queue_.size() < last_high_mark_ * shrink_factor_) {
                // 用move迭代器高效转移（支持move-only类型，无拷贝)
                // 如果元素类型不可 move，可fallback到常规 copy 构造法
                std::deque<T> newq(std::make_move_iterator(queue_.begin()),
                                   std::make_move_iterator(queue_.end()));
                queue_.swap(newq);
                last_high_mark_ = queue_.size();// 空时会归零
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cond_empty_;
    std::deque<T> queue_;

    // shrink参数及高水位
    const size_t shrink_check_interval_;
    const float shrink_factor_;
    size_t op_count_;
    size_t last_high_mark_;
};