#pragma once


#include <spdlog/spdlog.h>
#include <memory>
#include <string>

// 兼容性的spdlog异步logger创建函数
std::shared_ptr<spdlog::logger> gain_logger(const std::string& name);

void ResetLoggerPattern();

void LogMemorySnapshot(const std::string& context = "");

void pause_for_check(const std::string& msg);