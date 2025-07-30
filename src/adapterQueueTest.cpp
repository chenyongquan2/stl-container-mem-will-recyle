#include "adapterQueueTest.h"
#include "adapterQueue.h"


#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <optional>
#include <cassert>
#include <spdlog/spdlog.h>
#include "util.h"

// 伪造较大数据类型用于检验自动shrink
struct OneKBData {
    char buf[1024];
    OneKBData() { memset(buf, 0xFE, sizeof(buf)); }
    OneKBData(const OneKBData&) = default;
    OneKBData& operator=(const OneKBData&) = default;
};


void TestAdapterQueueMultiThreadAndAdapterAvaliable() {
    using Data = OneKBData;
    //AutoShrinkBlockingQueue<Data> q(100, 0.2f); // 每100次pop检测，低于高水位20%时shrink
    AutoShrinkBlockingQueue<Data> q(500, 0.2f); // 每100次pop检测，低于高水位20%时shrink

    const int N_PRODUCERS = 20;
    const int N_CONSUMERS = 1;
    const int PRODUCE_CNT = 100000;

    std::atomic<int> total_push{0}, total_pop{0};

    // 生产者线程
    std::vector<std::thread> producers;
    for(int i=0;i<N_PRODUCERS;++i) {
        producers.emplace_back([&]{
            for(int j=0; j<PRODUCE_CNT; ++j) {
                q.push(Data());
                ++total_push;
                if(j % 500 == 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // 消费者线程
    std::vector<std::thread> consumers;
    for(int i=0;i<N_CONSUMERS;++i) {
        consumers.emplace_back([&]{
            int popcount = 0;
            while(total_pop < N_PRODUCERS * PRODUCE_CNT) {
                std::optional<Data> v = q.try_pop();
                if(v) {
                    ++total_pop;
                    ++popcount;
                    if(popcount == 4000)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(20));
                }
            }
        });
    }

    // for(int i=0; i<10; ++i) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(200));
    //     SPDLOG_INFO("[Status] queue size={}", q.size());
    //     LogMemorySnapshot("Before thread join");
    // }

    SPDLOG_INFO("[Before thread join] queue size={}", q.size());
    LogMemorySnapshot("Before thread join");

    for(auto& th:producers) th.join();
    for(auto& th:consumers) th.join();

    SPDLOG_INFO("[After thread join] queue size={}", q.size());
    LogMemorySnapshot("After thread join");

    // 校验
    if(int(total_push) != N_PRODUCERS*PRODUCE_CNT) {
        SPDLOG_ERROR("[FAIL] Total push: {}, expect {}", int(total_push), N_PRODUCERS*PRODUCE_CNT);
    }
    else
    {
        SPDLOG_INFO("[OK] Total push: {}, expect {}", int(total_push), N_PRODUCERS*PRODUCE_CNT);
    }
    if(int(total_pop) != N_PRODUCERS*PRODUCE_CNT) {
        SPDLOG_ERROR("[FAIL] Total pop: {}, expect {}", int(total_pop), N_PRODUCERS*PRODUCE_CNT);
    }
    else
    {
        SPDLOG_INFO("[OK] Total pop: {}, expect {}", int(total_pop), N_PRODUCERS*PRODUCE_CNT);
    }

    if(!q.empty()) {
        SPDLOG_ERROR("[FAIL] Queue should be empty but size={}", q.size());
    }
    else
    {
        SPDLOG_INFO("[OK] Queue is empty");
    }

    auto o = q.try_pop();
    if(o) {
        SPDLOG_ERROR("[FAIL] Queue try_pop after empty should be nullopt");
    }
    else
    {
        SPDLOG_ERROR("[OK] Queue try_pop after empty is nullopt");
    }

    SPDLOG_INFO("[PASS] Total push/pop checked. Queue empty. Now check memory shrink after high peak...");
    LogMemorySnapshot("Pass all push/pop checked. Queue empty now");

    //==============================
    //Test case2:
    SPDLOG_INFO("=============[Test case2]=================");

    // 再次高峰+清空，检验shrinking
    for(int k=0;k<PRODUCE_CNT;++k)
    {
        q.push(Data());
    } 
    SPDLOG_INFO("[Test] After fill 10k, size={}", q.size());
    LogMemorySnapshot("[Test] After fill 10k");

    for(int k=0;k<PRODUCE_CNT;++k)
    {
        q.pop();
    }
    SPDLOG_INFO("[Test] After pop all, size={}", q.size());
    LogMemorySnapshot("[Test] After pop all");

    SPDLOG_INFO("[PASS] All test finished!");
}

void TestBenchmark()
{

}

void TestAdapterQueue()
{
    //测试多线程+弹性伸缩所占用的内存
    TestAdapterQueueMultiThreadAndAdapterAvaliable();


}