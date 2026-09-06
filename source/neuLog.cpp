#include "neuLog.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/base_sink.h>

namespace neurender {

std::mutex NeuLog::s_LogMutex;
std::vector<std::pair<std::string, int>> NeuLog::s_Logs;
std::vector<NeuLog::LogCallback> NeuLog::s_Callbacks;

template<typename Mutex>
class GuiSink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        std::string str = fmt::to_string(formatted);
        NeuLog::RecordLog(str, static_cast<int>(msg.level));
    }

    void flush_() override {}
};

void NeuLog::Init() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    auto gui_sink = std::make_shared<GuiSink<std::mutex>>();
    gui_sink->set_pattern("[%H:%M:%S] [%^%l%$] %v");

    std::vector<spdlog::sink_ptr> sinks { console_sink, gui_sink };
    auto logger = std::make_shared<spdlog::logger>("NeuTracingRender", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger);
    LOG_I("NeuLog initialized.");
}

void NeuLog::Shutdown() {
    spdlog::shutdown();
}

void NeuLog::AddCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(s_LogMutex);
    s_Callbacks.push_back(cb);
}

void NeuLog::RecordLog(const std::string& msg, int level) {
    std::lock_guard<std::mutex> lock(s_LogMutex);
    s_Logs.push_back({msg, level});
    if (s_Logs.size() > 500) {
        s_Logs.erase(s_Logs.begin(), s_Logs.begin() + 50);
    }
    for (auto& cb : s_Callbacks) {
        cb(msg, static_cast<spdlog::level::level_enum>(level));
    }
}

std::vector<std::pair<std::string, int>> NeuLog::GetRecentLogs() {
    std::lock_guard<std::mutex> lock(s_LogMutex);
    return s_Logs;
}

void NeuLog::ClearLogs() {
    std::lock_guard<std::mutex> lock(s_LogMutex);
    s_Logs.clear();
}

} // namespace neurender
