#include "boost-lock-free-queue.h"

#include <iostream>
#include <boost/lockfree/queue.hpp>
#include <vector>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include "util.h"

struct OneKBData {
    char buf[1024]; // 1KB
};

void lockfree_queue_threadsafe_demo(size_t n_producers, size_t n_consumers, size_t push_per_producer)
{
    boost::lockfree::queue<OneKBData> q; // 不要加capacity
    std::atomic<size_t> total_push{0};
    std::atomic<size_t> total_pop{0};
    std::atomic<size_t> fail_push{0};
    std::atomic<size_t> fail_pop{0};

    // 生产者线程
    std::vector<std::thread> producers;
    for(size_t pi=0; pi<n_producers; ++pi){
        producers.emplace_back([&, pi]{
            for(size_t i=0; i<push_per_producer; ++i){
                OneKBData val {};
                // 用内容填充（可选）:
                snprintf(val.buf, sizeof(val.buf), "producer-%zu, idx=%zu", pi, i);

                if(q.push(val)){
                    total_push.fetch_add(1, std::memory_order_relaxed);
                    //SPDLOG_INFO("[Producer-{}] push {}", pi, val);
                } else {
                    fail_push.fetch_add(1, std::memory_order_relaxed);
                    SPDLOG_ERROR("[Producer-{}] push {} failed!", pi, i);
                    // push 没失败符合预期
                    // fail push = 0，正常，因为你用了无固定容量的 boost::lockfree::queue<int>，只要内存允许基本不会push失败。
                    SPDLOG_INFO("Is lock free:{}", q.is_lock_free());
                }
                // 让出时间片，模拟并发
                if (i % 1000 == 0) std::this_thread::yield();
            }
            SPDLOG_INFO("[Producer-{}] Finished.", pi);
        });
    }

    // 消费者线程
    std::vector<std::thread> consumers;
    std::atomic<bool> producing_done{false};
    for(size_t ci=0; ci<n_consumers; ++ci){
        consumers.emplace_back([&, ci]{
            while(!producing_done.load() || !q.empty()){
                OneKBData val {};
                if(q.pop(val)){
                    total_pop.fetch_add(1, std::memory_order_relaxed);
                    //SPDLOG_INFO("[Consumer-{}] pop {}", ci, val);
                } else {
                    //fail pop的真实含义： 某一时刻，这个consumer试图弹一条数据，但队列为空（或其它线程刚好把那一项弹走了），pop失败，并不会影响队列已添加的数据。
                    //只要最终**(total pop + fail pop) ≥ total push**，或者准确说，只要total pop == total push且所有数据都被消费掉，没有pop出多出来的元素，就没有问题。
                    fail_pop.fetch_add(1, std::memory_order_relaxed);
                    // 这里可以sleep一下，避免空转
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    SPDLOG_WARN("Pop Failed occured, Is lock free:{}", q.is_lock_free());
                }
            }
            SPDLOG_INFO("[Consumer-{}] Finished.", ci);
        });
    }
    // 等待所有生产者
    for(auto& t : producers) 
    {
        t.join();
    }
    producing_done = true;
    SPDLOG_INFO("All producers finished!");
    // 等待所有消费者
    for(auto& t : consumers)
    {
        t.join();
    }

    SPDLOG_INFO("All consumers finished!");
    // . 核心判断线程安全的标准
    // push总数=pop总数：确认无丢失、无重复。
    // fail push=0：确认无容量限制下，扩容也没问题。
    // 存在 fail pop，不影响数据完整性，纯属“抢空、空转”特性
    // 打印统计
    SPDLOG_INFO("Stat: total push = {}, fail push = {}.", total_push.load(), fail_push.load());
    SPDLOG_INFO("Stat: total pop  = {}, fail pop  = {}.", total_pop.load(), fail_pop.load());
    SPDLOG_INFO("Is total push == total pop: {}", total_push.load() == total_pop.load());
    //验证一下内存占用
    pause_for_check("Thread safe test finished");

    //push过分配的内存节点，即使pop后空间也不会被释放给操作系统，而是由队列管理的内存池继续持有，直到队列析构。高峰时内存越大，后面就一直占这么多。

    // 适合与不适合的场景
    // 适合实时性强、瞬时高流量但略有空间冗余的场景。
    // 不适合频繁大起大落、且希望内存能自动shrinking收回的场景；
    // 此类需求建议定期销毁重建队列对象。
}

void TestBoostLockFreeQueueThreadSafe()
{
    // 3生产者、2消费者，每生产者push 1000条数据
    //lockfree_queue_threadsafe_demo(10, 10, 10000);

    //改大一下生产者的速度，降低消费者的速度,试一下pop是否还会有失败
    lockfree_queue_threadsafe_demo(20*5, 1, 100000);
}

int TestBoostLockFreeQueueSizeCanExpand() {
    //只要你不指定 capacity<>，就会走「动态 new 内存扩容」模式，可以持续扩容（非fixed-szed情况下），但此模式的 push 并非100% lock-free，性能、实时性略逊于固定容量方式。

    // 注意！不要写 capacity<>，用默认queue!
    boost::lockfree::queue<int> q;

    SPDLOG_INFO("Is lock free:{}", q.is_lock_free());

    const size_t N = 1000000; // 推入100万条

    size_t success = 0;
    size_t fail = 0;

    for (size_t i = 0; i < N; ++i) {
        if(q.push(static_cast<int>(i))) {
            ++success;
        } else {
            ++fail;
        }
    }

    SPDLOG_INFO("Push finished, success = {}, fail = {}", success, fail);

    //验证一下内存占用
    pause_for_check("Push finished");


    // 验证弹出
    int value = 0;
    size_t popcnt = 0;
    while(q.pop(value)) {
        ++popcnt;
    }

    SPDLOG_INFO("Pop finished, total pop = {}" , popcnt);

    pause_for_check("Pop finished");


    SPDLOG_INFO("Is lock free:{}", q.is_lock_free());

    return 0;
}