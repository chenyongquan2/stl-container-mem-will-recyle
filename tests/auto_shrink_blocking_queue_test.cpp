#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <algorithm>
#include <optional>
#include "adapterQueue.h"

using namespace std::chrono_literals;

// ========== 基础操作 ==========

TEST(AutoShrinkBlockingQueueTest, PushPopSingleThread) {
    AutoShrinkBlockingQueue<int> q;
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_TRUE(q.empty());
}

TEST(AutoShrinkBlockingQueueTest, TryPopWhenEmpty) {
    AutoShrinkBlockingQueue<int> q;
    auto v = q.try_pop();
    EXPECT_FALSE(v.has_value());
}

TEST(AutoShrinkBlockingQueueTest, TryPopNormal) {
    AutoShrinkBlockingQueue<std::string> q;
    q.push("hello");
    auto r = q.try_pop();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "hello");
    EXPECT_TRUE(q.empty());
}

// ========== move-only类型支持 ==========

struct MoveOnly {
    int x;
    explicit MoveOnly(int xx) : x(xx) {}
    MoveOnly(MoveOnly&& o) noexcept : x(o.x) { o.x = 0; }
    MoveOnly& operator=(MoveOnly&& o) noexcept { x = o.x; o.x = 0; return *this; }
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
};

TEST(AutoShrinkBlockingQueueTest, CanPushPopMoveOnlyStruct) {
    AutoShrinkBlockingQueue<MoveOnly> q;
    q.push(MoveOnly(99));
    MoveOnly v = q.pop();
    EXPECT_EQ(v.x, 99);
}

TEST(AutoShrinkBlockingQueueTest, CanPushMoveOnlyUniquePtr) {
    AutoShrinkBlockingQueue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(42));
    auto out = q.pop();
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, 42);
}

// ========== 阻塞测试 ==========

TEST(AutoShrinkBlockingQueueTest, PopBlocksUntilPush) {
    AutoShrinkBlockingQueue<int> q;
    std::atomic<bool> started{false}, popped{false};
    std::thread t([&]{
        started = true;
        int v = q.pop();
        EXPECT_EQ(v, 123);
        popped = true;
    });
    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(50ms); // 保证线程进入等待
    EXPECT_FALSE(popped);
    q.push(123);
    t.join();
    EXPECT_TRUE(popped);
}

// ========== 多线程并发 ==========

TEST(AutoShrinkBlockingQueueTest, MultiThreadedPushPop) {
    AutoShrinkBlockingQueue<int> q;
    constexpr int N = 500;
    std::vector<int> pushed, popped;
    for (int i = 0; i < N; ++i) pushed.push_back(i);

    std::thread producer([&]{ for (int v : pushed) q.push(v); });
    std::thread consumer([&]{ for (int i = 0; i < N; ++i) popped.push_back(q.pop()); });

    producer.join();
    consumer.join();

    std::sort(popped.begin(), popped.end());
    EXPECT_EQ(popped, pushed);
}

TEST(AutoShrinkBlockingQueueTest, MultiThreadedTryPopStress) {
    AutoShrinkBlockingQueue<int> q;
    constexpr int producerN = 8, consumerN = 8, numPerProducer = 64;
    std::atomic<int> produced{0}, consumed{0};
    std::vector<std::thread> threads;
    std::vector<int> output;
    std::mutex output_mutex;

    // 生产者
    for (int i = 0; i < producerN; ++i) {
        threads.emplace_back([&, i]{
            for (int j = 0; j < numPerProducer; ++j) {
                q.push(i * numPerProducer + j);
                ++produced;
            }
        });
    }
    // 消费者
    for (int i = 0; i < consumerN; ++i) {
        threads.emplace_back([&]{
            while (consumed < producerN * numPerProducer) {
                auto v = q.try_pop();
                if (v) {
                    {
                        std::lock_guard<std::mutex> lock(output_mutex);
                        output.push_back(*v);
                    }
                    ++consumed;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(produced, consumed);
    EXPECT_EQ(output.size(), producerN * numPerProducer);
}

// ========== shrink 行为（内存&高水位回收） ==========

// 测试 shrink 能正确归零 high_mark 和队列内容，符合 STL shrink 语义
TEST(AutoShrinkBlockingQueueTest, ShrinkHighMarkOnEmpty) {
    AutoShrinkBlockingQueue<int> q(4, 0.2f); // 低 interval 方便测试
    for (int i = 0; i < 20; ++i) q.push(i);
    EXPECT_EQ(q.last_high_mark(), 20u);

    for (int i = 0; i < 20; ++i) q.pop();

    // 多 pop 几轮确保 interval 计数归零，触发 shrink
    for (int i = 0; i < 8; ++i) q.try_pop();
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.last_high_mark(), 0u); // 归零！
}

TEST(AutoShrinkBlockingQueueTest, ShrinkHighMarkOnLowWater) {
    AutoShrinkBlockingQueue<int> q(3, 0.33f);
    for (int i = 0; i < 12; ++i) q.push(i);
    EXPECT_EQ(q.last_high_mark(), 12u);

    // 连续pop到剩4，低于12*0.33=4（刚好等于也不会 shrink，应再少一个）
    for (int i = 0; i < 9; ++i) q.pop();
    // 现在剩3，已经小于临界，op_count=9，interval=3应已多次检查
    // 多 pop 能确保 interval 检查被触发
    for (int i = 0; i < 6; ++i) q.try_pop();
    // 只要 shrink 检查被走，high_mark 应归零或对齐实际
    EXPECT_LE(q.last_high_mark(), q.size());
}

TEST(AutoShrinkBlockingQueueTest, HighMarkTracksMax) {
    AutoShrinkBlockingQueue<int> q;
    EXPECT_EQ(q.last_high_mark(), 0u);
    q.push(1); q.push(2); q.push(3);
    EXPECT_EQ(q.last_high_mark(), 3u);
    q.pop(); q.pop();                 // size=1
    q.push(4);                        // size=2
    EXPECT_EQ(q.last_high_mark(), 3u);
    q.push(5);                        // size=3
    EXPECT_EQ(q.last_high_mark(), 3u); // 推入时没超过
    q.push(6);                        // size=4
    EXPECT_EQ(q.last_high_mark(), 4u); // 这里才增长
}

// ========== 其它边界与API一致性验证 ==========

TEST(AutoShrinkBlockingQueueTest, EmptyAndSizeThreadSafe) {
    AutoShrinkBlockingQueue<int> q;
    std::atomic<bool> running{true};
    std::thread writer([&]() {
        int v = 0;
        while (running) {
            q.push(v++);
            std::this_thread::sleep_for(1ms);
        }
    });
    std::thread reader([&]() {
        while (running) {
            q.try_pop();
            std::this_thread::sleep_for(1ms);
        }
    });
    for (int i = 0; i < 32; ++i) {
        (void)q.empty();
        (void)q.size();
        std::this_thread::sleep_for(1ms);
    }
    running = false;
    writer.join();
    reader.join();
}

TEST(AutoShrinkBlockingQueueTest, PopPushAlternating) {
    AutoShrinkBlockingQueue<int> q;
    for (int i = 0; i < 8; ++i) {
        q.push(i);
        EXPECT_EQ(q.pop(), i);
    }
    EXPECT_TRUE(q.empty());
}