#include "util.h"
#include <windows.h>
#include <shlwapi.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/hourly_file_sink.h>
#include <memory>
#include <string>
#include <psapi.h>

// 兼容性的spdlog异步logger创建函数
std::shared_ptr<spdlog::logger> gain_logger(const std::string& name)
{
    // 1. 用ANSI/char确保32/64位下都统一
    char spath[MAX_PATH] = { 0 };
    // 获取exe全路径（ANSI版，避免TCHAR切换）
    GetModuleFileNameA(nullptr, spath, MAX_PATH);

    // 2. 转成fs::path，拼出logs目录和日志文件名
    std::filesystem::path module_path(spath);
    std::filesystem::path log_dir = module_path.parent_path() / "logs";
    if (!std::filesystem::exists(log_dir))
        std::filesystem::create_directories(log_dir);
    std::filesystem::path log_path = log_dir / (name + ".log");

    // 3. 主动初始化spdlog异步线程池（如需反复创建建议移至程序全局只调一次）
    static std::once_flag flag;
    std::call_once(flag, []() {
        spdlog::init_thread_pool(8192, 1); // 8k items, 1 log thread
    });

    // 4. 创建异步logger
    auto async_logger = spdlog::create_async<spdlog::sinks::hourly_file_sink_mt>(name, log_path.string());
    async_logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
    async_logger->flush_on(spdlog::level::info);

    return async_logger;
}

// 内存统计结构体
struct MemoryStats {
    double rss_mib = 0.0;    // Resident Set Size (工作集)
    double commit_mib = 0.0; // Committed Memory (提交内存)
    size_t page_faults = 0;  // 页面错误计数
    size_t peak_working_set = 0; // 峰值工作集大小
};

// 获取内存统计信息
MemoryStats GetMemoryStats() {
    MemoryStats stats;
    
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), 
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), 
                           sizeof(pmc))) {
        // 基础内存信息
        // 进程当前使用的物理内存（包括共享 DLL 等）
            // 对应：
            // mimalloc: rss
            // 任务管理器: 工作集（Working Set）
        stats.rss_mib = static_cast<double>(pmc.WorkingSetSize) / (1024 * 1024);

        //  进程私有的虚拟内存（实际已经提交的部分）
            // 对应：
            // mimalloc: commit
            // 任务管理器: 提交大小（Commit Size）
  
        // 注意：不是专用工作集，也不是保留虚拟内存，而是实际 已提交使用的虚拟内存。
        stats.commit_mib = static_cast<double>(pmc.PrivateUsage) / (1024 * 1024);
        
        // 附加内存指标
        stats.page_faults = pmc.PageFaultCount;

        // 峰值工作集大小, 进程生命周期中曾经达到的最大工作集大小
        stats.peak_working_set = pmc.PeakWorkingSetSize / (1024 * 1024); // MiB
    } else {
        DWORD error = GetLastError();
        SPDLOG_ERROR("GetProcessMemoryInfo failed. Error code: {}", error);
    }
    
    return stats;
}

// 格式化内存统计信息
std::string FormatMemoryStats(const MemoryStats& stats) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "rss(working set): " << stats.rss_mib << " MiB";
    oss << ", commit size: " << stats.commit_mib << " MiB";
    //oss << ", page faults: " << stats.page_faults;
    oss << ", peak WorkingSetSize: " << stats.peak_working_set << " MiB";
    return oss.str();
}

// 记录内存快照
void LogMemorySnapshot(const std::string& context/* = ""*/) 
{
    static size_t snapshot_count = 0;
    snapshot_count++;
    
    auto stats = GetMemoryStats();
    std::string message = FormatMemoryStats(stats);
    
    if (!context.empty()) {
        message = context + " - " + message;
    }
    
    SPDLOG_INFO("[Snapshot #{}] {}", snapshot_count, message);
}