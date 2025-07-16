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
    // SPDLOG_INFO("\n>>>> {}，请用任务管理器/`top`等工具观察进程内存。按下回车继续...", msg);
    // std::cin.get();
}

struct PartInfoLog
{
    PartInfoLog(const std::string &name)
        :name_(name)
    {
        SPDLOG_INFO("[Begin] Part:{}===========", name_);
    }

    ~PartInfoLog()
    {
        SPDLOG_INFO("[End] Part:{}===========", name_);
    }

    std::string name_;
};

void DoTest() 
{
    {
        PartInfoLog log("vector test");
        std::vector<Data> v;
        
        for (int i = 0; i < 100000; ++i) { // 共约100MB
            v.emplace_back();
        }
        pause_for_check("std::vector 插入10万对象后");

        v.clear();
        SPDLOG_INFO("std::vector clear()完成，元素全部析构，但capacity未变");
        pause_for_check("std::vector clear()后");

        v.shrink_to_fit();
        SPDLOG_INFO("std::vector shrink_to_fit()后，capacity建议恢复和size一样，部分实现会释放一部分空间");
        pause_for_check("std::vector shrink_to_fit()后");

        std::vector<Data>().swap(v);
        SPDLOG_INFO("std::vector swap空对象后，内存完全归还，vector成为全新空对象");
        pause_for_check("std::vector swap空对象后");
    }

    {
        PartInfoLog log("deque test");
        // === std::deque 测试 ===
        std::deque<Data> dq;

        for (int i = 0; i < 100000; ++i) { // 共约100MB
            dq.emplace_back();
        }
        pause_for_check("std::deque 插入10万对象后");

        dq.clear();
        SPDLOG_INFO("std::deque clear()完成，元素全部析构，但buffer未必被释放");
        pause_for_check("std::deque clear()后");

        dq.shrink_to_fit();
        //std::deque<Data>().swap(dq);      // 强制释放空间
        SPDLOG_INFO("std::deque shrink_to_fit空对象后，理论上已完全释放内存buffer");
        pause_for_check("std::deque shrink_to_fit空对象后");
    }

    {
        PartInfoLog log("queue test");
        // === std::queue 测试 ===
        std::queue<Data> q;
        
        for (int i = 0; i < 100000; ++i) {
            q.emplace();
        }
        //pause_for_check("std::queue 插入10万对象后");

        while (!q.empty()) {
            q.pop();
        }
        SPDLOG_INFO("std::queue 已pop掉所有元素，但底层buffer未必释放");
        pause_for_check("std::queue pop清空后");
       
        {
            std::queue<Data> empty;
            q.swap(empty); // 强制释放所有buffer
            SPDLOG_INFO("std::queue swap空对象后，此时还没退出本来对象的作用域");
            pause_for_check("std::queue swap空对象后，此时还没退出本来对象的作用域");
        }
        
        SPDLOG_INFO("[after]std::queue swap空对象后，理论上已完全释放内存buffer");
        pause_for_check("[after]std::queue swap空对象后");
    }
    SPDLOG_INFO("will sleep所有测试已结束，按回车退出。");

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