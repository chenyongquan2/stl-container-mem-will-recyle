#include "threadSafeQueue.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <mutex>
#include <optional>
#include <chrono>

// ====================== 基本单线程功能 =============================

TEST(ThreadSafeQueueTest, EmptyOnConstruction) {
    ThreadSafeQueue<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(ThreadSafeQueueTest, PushPopSingleThread) {
    ThreadSafeQueue<int> q;
    q.push(42);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);

    int val = q.pop();
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(ThreadSafeQueueTest, TryPopOnEmptyReturnsNullopt) {
    ThreadSafeQueue<int> q;
    auto res = q.try_pop();
    EXPECT_FALSE(res.has_value());
}

TEST(ThreadSafeQueueTest, TryPopReturnsFrontElement) {
    ThreadSafeQueue<int> q;
    q.push(100);
    auto res = q.try_pop();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 100);
    EXPECT_TRUE(q.empty());
}

TEST(ThreadSafeQueueTest, PopWithMultipleElementsOrder) {
    ThreadSafeQueue<int> q;
    for (int i = 1; i <= 3; ++i) q.push(i);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
    EXPECT_TRUE(q.empty());
}

// ====================== 移动语义功能 =============================
struct Movable {
    int value;
    bool moved = false;
    Movable(int v): value(v) {}
    Movable(Movable&& o) noexcept : value(o.value), moved(true) { o.value = 0; }
    Movable& operator=(Movable&& o) noexcept {
        if (this != &o) {
            value = o.value; moved = true; o.value = 0;
        }
        return *this;
    }
    Movable(const Movable&) = delete;
    Movable& operator=(const Movable&) = delete;
};

TEST(ThreadSafeQueueTest, PushRvalueMoveChecked) {
    ThreadSafeQueue<Movable> q;
    Movable m(123);
    q.push(std::move(m));
    auto popped = q.try_pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped->value, 123);
    EXPECT_TRUE(popped->moved); // 对象被通过移动构造
    EXPECT_EQ(m.value, 0);      // 源对象已被move置零
}

// ====================== 多线程功能&同步正确性 =============================

TEST(ThreadSafeQueueTest, ProducerConsumerDataIntegrity) {
    constexpr int thread_count = 8;
    constexpr int per_thread_items = 1000;
    const int total = thread_count * per_thread_items;

    ThreadSafeQueue<int> q;
    std::atomic<int> sum{0};
    std::vector<std::thread> producers;
    std::set<int> all_results;
    std::mutex set_mutex;

    // 启动生产者
    for (int t = 0; t < thread_count; ++t) {
        producers.emplace_back([&q, t] {
            for (int i = 0; i < per_thread_items; ++i) {
                q.push(t * per_thread_items + i);
            }
        });
    }

    // 启动消费者
    std::vector<std::thread> consumers;
    for (int t = 0; t < thread_count; ++t) {
        consumers.emplace_back([&q, &sum, &all_results, &set_mutex, per_thread_items] {
            for (int i = 0; i < per_thread_items; ++i) {
                int val = q.pop();
                sum += val;
                std::lock_guard<std::mutex> lock(set_mutex);
                all_results.insert(val);
            }
        });
    }

    for (auto& th : producers) th.join();
    for (auto& th : consumers) th.join();

    // 所有producer产出的数据都应被消费且无重复
    EXPECT_EQ(all_results.size(), total);
    int expect_sum = total * (total - 1) / 2;
    EXPECT_EQ(sum, expect_sum);
    EXPECT_TRUE(q.empty());
}

// ========== 多线程 try_pop 验证可用性和线程安全性 ==========
TEST(ThreadSafeQueueTest, TryPopMultiThreaded) {
    ThreadSafeQueue<int> q;
    constexpr int N = 1000;
    for (int i = 0; i < N; ++i) q.push(i);

    std::atomic<int> popped{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&q, &popped]() {
            while (true) {
                auto v = q.try_pop();
                if (!v) break;
                ++popped;
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(popped, N);
    EXPECT_TRUE(q.empty());
}

// ========== pop阻塞行为测试 ==========
TEST(ThreadSafeQueueTest, PopBlocksUntilPush) {
    ThreadSafeQueue<int> q;
    std::atomic<bool> started{false};
    int result = 0;
    std::thread consumer([&]() {
        started = true;
        result = q.pop();
    });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 更确保阻塞
    q.push(999);
    consumer.join();
    EXPECT_EQ(result, 999);
}

// ========== 队列空/size安全并发访问检测（非功能性压力） ==========
TEST(ThreadSafeQueueTest, SizeAndEmptyMultiThreadedSafe) {
    ThreadSafeQueue<int> q;
    constexpr int N = 10000;
    std::atomic<bool> done{false};

    std::thread writer([&]() {
        for (int i = 0; i < N; ++i) q.push(i);
        done = true;
    });
    while (!done) {
        q.size();
        q.empty();
    }
    writer.join();
    EXPECT_GE(q.size(), 0u); // 不作功能断言，只要没deadlock
}

// ========== 边界和异常用例 ==========
TEST(ThreadSafeQueueTest, PopAfterClearPush) {
    ThreadSafeQueue<std::string> q;
    q.push("abc");
    EXPECT_EQ(q.pop(), "abc");
    q.push("def");
    EXPECT_EQ(q.pop(), "def");
    EXPECT_TRUE(q.empty());
}

// ========== 可移动但不可拷贝的类型 ==========
TEST(ThreadSafeQueueTest, OnlyMovableType) {
    ThreadSafeQueue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(1));
    auto uptr = q.pop();
    EXPECT_TRUE(uptr);
    EXPECT_EQ(*uptr, 1);
    EXPECT_TRUE(q.empty());
}
