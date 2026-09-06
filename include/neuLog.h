#pragma once
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace neurender {

class NeuLog {
public:
    static void Init();
    static void Shutdown();

    using LogCallback = std::function<void(const std::string&, spdlog::level::level_enum)>;
    static void AddCallback(LogCallback cb);

    static std::vector<std::pair<std::string, int>> GetRecentLogs();
    static void ClearLogs();
    static void RecordLog(const std::string& msg, int level);

private:
    static std::mutex s_LogMutex;
    static std::vector<std::pair<std::string, int>> s_Logs;
    static std::vector<LogCallback> s_Callbacks;
};

} // namespace neurender

#define LOG_T(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_D(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_I(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_W(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_E(...) SPDLOG_ERROR(__VA_ARGS__)
