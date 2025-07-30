#include <benchmark/benchmark.h>
#include "adapterQueue.h"
#include <memory>
#include <atomic>
#include <barrier>
#include <thread>
#include <spdlog/spdlog.h>
// =================== Fixture定义（只用于单线程静态数据，不做全局共享） ===================
class ASBQFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& state) override {
        // 只用于单线程用例
        q = std::make_unique<AutoShrinkBlockingQueue<int>>();
        fatq = std::make_unique<AutoShrinkBlockingQueue<FatObj>>();
        produced = 0;
        consumed = 0;
    }
    void TearDown(const benchmark::State&) override {
        q.reset(); fatq.reset();
    }
    struct FatObj { char buf[4096]; int id; };
    std::unique_ptr<AutoShrinkBlockingQueue<int>> q;
    std::unique_ptr<AutoShrinkBlockingQueue<FatObj>> fatq;
    std::atomic<int64_t> produced{0};
    std::atomic<int64_t> consumed{0};
};

// =================== 多线程共享对象区（全局静态定义） ===================
static std::unique_ptr<AutoShrinkBlockingQueue<int>> g_int_queue;
static std::unique_ptr<AutoShrinkBlockingQueue<ASBQFixture::FatObj>> g_fat_queue;
static std::unique_ptr<std::barrier<>> g_barrier;
static std::atomic<int64_t> g_produced{0};
static std::atomic<int64_t> g_consumed{0};

// ========================== 1. 单线程 Push ==========================
BENCHMARK_DEFINE_F(ASBQFixture, SinglePush)(benchmark::State& state) {
    for (auto _ : state)
        q->push(42);
}
BENCHMARK_REGISTER_F(ASBQFixture, SinglePush);

// ========================== 2. 单线程 TryPop ==========================
BENCHMARK_DEFINE_F(ASBQFixture, SingleTryPop)(benchmark::State& state) {
    for (int i = 0; i < state.range(0); ++i)
        q->push(42);
    for (auto _ : state)
        benchmark::DoNotOptimize(q->try_pop());
}
BENCHMARK_REGISTER_F(ASBQFixture, SingleTryPop)->Arg(1000)->Arg(10000)->Arg(100000);

// ========================== 3. 单线程 Push + TryPop交替 ==========================
BENCHMARK_DEFINE_F(ASBQFixture, SinglePushTryPop)(benchmark::State& state) {
    for (auto _ : state) {
        q->push(123);
        benchmark::DoNotOptimize(q->try_pop());
    }
}
BENCHMARK_REGISTER_F(ASBQFixture, SinglePushTryPop);

// ========================== 4. 多线程 Push (竞争) ==========================
BENCHMARK_DEFINE_F(ASBQFixture, ParallelPush)(benchmark::State& state) {
    // 静态初始化全局队列和barrier
    if (state.thread_index() == 0) {
        g_int_queue = std::make_unique<AutoShrinkBlockingQueue<int>>();
        g_barrier = std::make_unique<std::barrier<>>(state.threads());
    }
    // spin直到资源ready
    while(!g_int_queue || !g_barrier) std::this_thread::yield();

    g_barrier->arrive_and_wait();
    for (auto _ : state)
        g_int_queue->push(42);

    // 清理
    g_barrier->arrive_and_wait();
    if (state.thread_index() == 0) {
        g_int_queue.reset();
        g_barrier.reset();
    }
}
BENCHMARK_REGISTER_F(ASBQFixture, ParallelPush)->Threads(2)->Threads(4)->Threads(8);

// ========================== 5. 多线程 TryPop (提前填充) ==========================
BENCHMARK_DEFINE_F(ASBQFixture, ParallelTryPop)(benchmark::State& state) {
    // 静态初始化共享队列+barrier（queue 提前填充）
    if (state.thread_index() == 0) {
        g_int_queue = std::make_unique<AutoShrinkBlockingQueue<int>>();
        g_barrier = std::make_unique<std::barrier<>>(state.threads());
        for (int i = 0; i < state.range(0); ++i)
            g_int_queue->push(42);
    }
    while(!g_int_queue || !g_barrier) std::this_thread::yield();

    //SPDLOG_INFO("Thread:{} Finish push, q_addr:{}", state.thread_index(), static_cast<const void*>(g_int_queue.get()));
    g_barrier->arrive_and_wait();
    //SPDLOG_INFO("Thread:{} Finish waiting, q_addr:{}", state.thread_index(), static_cast<const void*>(g_int_queue.get()));

    for (auto _ : state)
        benchmark::DoNotOptimize(g_int_queue->try_pop());

    g_barrier->arrive_and_wait();
    if (state.thread_index() == 0) {
        g_int_queue.reset();
        g_barrier.reset();
    }
}
BENCHMARK_REGISTER_F(ASBQFixture, ParallelTryPop)->Threads(2)->Threads(4)->Threads(8)->Arg(10000);

// ========================== 6. Producer-Consumer ==========================
BENCHMARK_DEFINE_F(ASBQFixture, ProducerConsumer)(benchmark::State& state) {
    int batch = state.range(0);
    // g_int_queue/g_barrier/g_produced/g_consumed全局
    if (state.thread_index() == 0) {
        g_int_queue = std::make_unique<AutoShrinkBlockingQueue<int>>();
        g_barrier = std::make_unique<std::barrier<>>(state.threads());
        g_produced.store(0, std::memory_order_relaxed);
        g_consumed.store(0, std::memory_order_relaxed);
    }
    while(!g_int_queue || !g_barrier) std::this_thread::yield();

    g_barrier->arrive_and_wait();

    if (state.thread_index() % 2 == 0) {
        // Producer
        for (auto _ : state) {
            for (int i = 0; i < batch; ++i) {
                g_int_queue->push(i);
                g_produced.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        // Consumer
        for (auto _ : state) {
            for (int i = 0; i < batch; ++i) {
                while (!g_int_queue->try_pop())
                    std::this_thread::yield();
                g_consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    g_barrier->arrive_and_wait();

    if (state.thread_index() == 0) {
        state.counters["produce_total"] = g_produced.load();
        state.counters["consume_total"] = g_consumed.load();
        state.counters["produce_rate"] = benchmark::Counter(
            static_cast<double>(g_produced.load()), benchmark::Counter::kIsRate);
        state.counters["consume_rate"] = benchmark::Counter(
            static_cast<double>(g_consumed.load()), benchmark::Counter::kIsRate);
        g_int_queue.reset();
        g_barrier.reset();
    }
}
BENCHMARK_REGISTER_F(ASBQFixture, ProducerConsumer)
    ->Threads(2)->Threads(4)->Threads(8)
    ->Arg(1)->Arg(10)->Arg(100);

// ========================== 7. Shrink策略 sweep（无共享资源） ==========================
BENCHMARK_DEFINE_F(ASBQFixture, ShrinkSweep)(benchmark::State& state) {
    size_t shrink_interval = state.range(0);
    float shrink_factor = static_cast<float>(state.range(1)) / 100.0f;
    AutoShrinkBlockingQueue<int> qtmp(shrink_interval, shrink_factor);
    for (auto _ : state)
        qtmp.push(42), benchmark::DoNotOptimize(qtmp.try_pop());
}
BENCHMARK_REGISTER_F(ASBQFixture, ShrinkSweep)
    ->Args({100,15})->Args({100,25})->Args({500,20})->Args({1000,20});

// ========================== 8. 大对象单线程/多线程 push/try_pop ==========================
// 单线程push
BENCHMARK_DEFINE_F(ASBQFixture, FatObjSinglePush)(benchmark::State& state) {
    FatObj obj{};
    obj.id = 666;
    for (auto _ : state)
        fatq->push(obj);
}
BENCHMARK_REGISTER_F(ASBQFixture, FatObjSinglePush);

// 单线程try_pop
BENCHMARK_DEFINE_F(ASBQFixture, FatObjSingleTryPop)(benchmark::State& state) {
    FatObj obj{};
    obj.id = 888;
    for (int i = 0; i < state.range(0); ++i)
        fatq->push(obj);
    for (auto _ : state)
        benchmark::DoNotOptimize(fatq->try_pop());
}
BENCHMARK_REGISTER_F(ASBQFixture, FatObjSingleTryPop)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();