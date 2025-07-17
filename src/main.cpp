#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <spdlog/sinks/hourly_file_sink.h>


#include <thread>
#include <deque>
#include <queue>
#include <iostream>
#include <string>


#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/hourly_file_sink.h>
#include <memory>
#include <string>

#include "util.h"

struct Data {
    char buf[1024]; // 1KB
};


void pause_for_check(const std::string& msg) {
    LogMemorySnapshot(msg);
    SPDLOG_INFO("");
    // SPDLOG_INFO("\n>>>> {}，请用任务管理器/`top`等工具观察进程内存。按下回车继续...", msg);
    // std::cin.get();
}

struct PartInfoLog
{
    PartInfoLog(const std::string &name)
        :name_(name)
    {
        SPDLOG_INFO("");
        SPDLOG_INFO("[Begin] Part:{}===========", name_);
    }

    ~PartInfoLog()
    {
        SPDLOG_INFO("[End] Part:{}===========", name_);
        SPDLOG_INFO("");
    }

    std::string name_;
};

void DoTest() 
{
    {
        PartInfoLog log("vector test");

        std::vector<Data> v;

        // 1. 填充数据
        for (int i = 0; i < 100000; ++i) {
            v.emplace_back();
        }
        pause_for_check("std::vector 插入10万对象后");

        // 2. 逐个pop_back清空
        size_t pops = 0;
        while (!v.empty()) {
            v.pop_back();
            ++pops;
            // if (pops % 10000 == 0) {
            //     SPDLOG_INFO("已pop_back移除 {} 个元素", pops);
            // }
        }
        SPDLOG_INFO("std::vector 逐个pop_back清空所有元素（共移除 {} 个），capacity未释放", pops);
        pause_for_check("std::vector 逐个pop_back清空后");

        // 3. 再插满一次演示capacity复用
        for (int i = 0; i < 100000; ++i) {
            v.emplace_back();
        }
        pause_for_check("std::vector 第二次插入10万对象后");

        // 4. clear方式清空，析构而不释放buffer
        v.clear();
        SPDLOG_INFO("std::vector clear()完成，所有元素析构但capacity未变");
        pause_for_check("std::vector clear()后");

        // 5. swap空对象，彻底回收所有buffer
        {
            std::vector<Data> empty;
            v.swap(empty);
            SPDLOG_INFO("std::vector swap空对象后，此时还没退出本来对象作用域");
            pause_for_check("std::vector swap空对象后，此时还没退出本来对象作用域");
        }
        SPDLOG_INFO("[after] std::vector swap空对象后，理论上已完全归还全部内存。");
        pause_for_check("[after] std::vector swap空对象后");
    }

    {
        PartInfoLog log("deque test");

        // === std::deque 测试 ===
        std::deque<Data> dq;
        for (int i = 0; i < 100000; ++i) { // 共约100MB
            dq.emplace_back();
        }
        pause_for_check("std::deque 插入10万对象后");

        // 方案一：逐个pop_back清空
        size_t pops = 0;
        while (!dq.empty()) {
            dq.pop_back();
            ++pops;
            // 这里推荐只在调试时打印log，否则10万行输出
            // if (pops % 10000 == 0) {
            //     SPDLOG_INFO("已pop_back移除 {} 个元素", pops);
            // }
        }
        SPDLOG_INFO("std::deque pop_back清空所有元素（共移除{}个），各节点析构，但块内的buffer未必释放", pops);
        pause_for_check("std::deque pop_back清空后");

        // 方案二：重新插入，再用clear清空（可选，与上面对比）
        for (int i = 0; i < 100000; ++i) {
            dq.emplace_back();
        }
        pause_for_check("std::deque 第二次插入10万对象后");

        dq.clear();
        SPDLOG_INFO("std::deque clear()完成，元素全部析构，但buffer未必被释放");
        pause_for_check("std::deque clear()后");

        // shrink_to_fit
        dq.shrink_to_fit();
        SPDLOG_INFO("std::deque shrink_to_fit后，理论上释放了多余buffer（但实现依赖库）");
        pause_for_check("std::deque shrink_to_fit后");

        // 强制swap空对象
        std::deque<Data>().swap(dq);
        SPDLOG_INFO("std::deque swap空对象后，理论上buffer全部归还操作系统");
        pause_for_check("std::deque swap空对象后");
    }

    {
        PartInfoLog log("queue test");
        // === std::queue 测试 ===
        std::queue<Data> q;
        for (int i = 0; i < 100000; ++i) {
            q.emplace();
        }
        pause_for_check("std::queue 插入10万对象后");

        // 1. 逐个pop清空
        size_t pops = 0;
        while (!q.empty()) {
            q.pop();
            ++pops;
            // if (pops % 10000 == 0) {
            //     SPDLOG_INFO("已pop移除 {} 个元素", pops);
            // }
        }
        SPDLOG_INFO("std::queue 逐个pop清空所有元素（共移除 {} 个），底层buffer未必释放", pops);
        pause_for_check("std::queue 逐个pop清空后");

        // 2. 再push一批数据
        for (int i = 0; i < 100000; ++i) {
            q.emplace();
        }
        pause_for_check("std::queue 第二次插入10万对象后");

        // 3. clear (自定义清空方法)它没有 clear()
        size_t clear_pops = 0;
        {
            // 模拟clear: 循环pop
            while (!q.empty()) {
                q.pop();
                ++clear_pops;
                // if (clear_pops % 10000 == 0) {
                //     SPDLOG_INFO("clear: 已pop移除 {} 个元素", clear_pops);
                // }
            }
        }
        SPDLOG_INFO("std::queue clear()完成（模拟，pop清空,因为他本身没有clear的方法），共移除 {} 个，buffer未必释放", clear_pops);
        pause_for_check("std::queue clear()（pop清空）后");

        // 4. swap空对象彻底释放空间
        {
            std::queue<Data> empty;
            q.swap(empty); // 强制释放所有buffer
            SPDLOG_INFO("std::queue swap空对象后，此时还没退出本来对象作用域");
            pause_for_check("std::queue swap空对象后，此时还没退出本来对象作用域");
        }
        SPDLOG_INFO("[after] std::queue swap空对象后，理论上已完全归还全部内存。");
        pause_for_check("[after] std::queue swap空对象后");
    }

    {
        PartInfoLog log("list test");
        std::list<Data> l;

        for (int i = 0; i < 100000; ++i) {
            l.emplace_back();
        }
        SPDLOG_INFO("std::list 插入10万对象后");
        pause_for_check("std::list 插入10万对象后");

        // 方案一：用pop逐个清空
        while (!l.empty()) {
            l.pop_front();
        }
        SPDLOG_INFO("std::list 用pop_front清空所有元素，理论上全部节点析构并释放");
        pause_for_check("std::list pop_front清空后");

        // 方案二：可尝试clear
        l.clear();
        SPDLOG_INFO("std::list clear()清空后");
        pause_for_check("std::list clear()清空后");

        // swap空对象
        {
            std::list<Data> empty;
            l.swap(empty); // 强制swap到一个空list
            SPDLOG_INFO("std::list swap空对象后，此时还没退出本来对象作用域");
            pause_for_check("std::list swap空对象后，此时还没退出本来对象作用域");
        }
        SPDLOG_INFO("[after] std::list swap空对象后，理论上已完全归还全部内存。");
        pause_for_check("[after] std::list swap空对象后");
    }

    {
        PartInfoLog log("map test");

        std::map<int, Data> m;
        for (int i = 0; i < 100000; ++i) {
            m.emplace(i, Data{});
        }
        SPDLOG_INFO("std::map 插入10万对象后");
        pause_for_check("std::map 插入10万对象后");

        // 方案一：单个erase
        for (int i = 0; i < 100000; ++i) {
            m.erase(i);
        }
        SPDLOG_INFO("std::map erase所有key后，理论上所有元素析构并释放节点内存（红黑树节点）");
        pause_for_check("std::map 单个erase全部元素后");

        // 方案二：直接clear，等价效果
        m.clear();
        SPDLOG_INFO("std::map clear()后（更高效，直接整棵树删空）");
        pause_for_check("std::map clear()后");

        // swap空对象
        {
            std::map<int, Data> empty;
            m.swap(empty); // 强制swap到一个空map
            SPDLOG_INFO("std::map swap空对象后，此时还没退出原map作用域");
            pause_for_check("std::map swap空对象后，此时还没退出原map作用域");
        }
        SPDLOG_INFO("[after] std::map swap空对象后，理论上全部节点已释放完毕。");
        pause_for_check("[after] std::map swap空对象后");
    }
    
    {
        PartInfoLog log("unordered_map test");
        std::unordered_map<int, Data> m;
        for (int i = 0; i < 100000; ++i) {
            m.emplace(i, Data{});
        }
        SPDLOG_INFO("std::unordered_map 插入10万对象后");
        pause_for_check("std::unordered_map 插入10万对象后");

        // 方案一：单个erase
        for (int i = 0; i < 100000; ++i) {
            m.erase(i);
        }
        SPDLOG_INFO("std::unordered_map erase所有key后，理论上所有元素析构并释放哈希桶节点的内存");
        pause_for_check("std::unordered_map 单个erase全部元素后");

        // 方案二：直接clear，等价效果
        m.clear();
        SPDLOG_INFO("std::unordered_map clear()后（更高效，清空所有桶及节点）");
        pause_for_check("std::unordered_map clear()后");

        // swap空对象
        {
            std::unordered_map<int, Data> empty;
            m.swap(empty); // 强制swap到一个空unordered_map
            SPDLOG_INFO("std::unordered_map swap空对象后，此时还没退出原map作用域");
            pause_for_check("std::unordered_map swap空对象后，此时还没退出原map作用域");
        }
        SPDLOG_INFO("[after] std::unordered_map swap空对象后，理论上全部元素节点已释放完毕。");
        pause_for_check("[after] std::unordered_map swap空对象后");
    }


    SPDLOG_INFO("所有测试已结束，按回车退出。");
    //std::cin.get();
}

int main(){
    // 初始化 spdlog
    //auto console = spdlog::stdout_color_mt("console");
    //spdlog::set_default_logger(console);
    //spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    spdlog::set_default_logger(gain_logger("sg_common"));
    spdlog::flush_on(spdlog::level::err);
    spdlog::flush_every(std::chrono::seconds(3));
    void ResetLoggerPattern();

    SPDLOG_INFO("main start");

    DoTest();
    //while loop
    // std::string line;
    // while (std::getline(std::cin, line)) {
    //     if (line == "exit") break;
    // }
    SPDLOG_INFO("main end");

    return 0;
        
}