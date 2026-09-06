#pragma once
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>

namespace neurender {

class Console {
public:
    Console() = delete;

    using CommandHandler = std::function<bool(const std::vector<std::string>&)>;

    static void Init();
    static void Shutdown();
    static void Update();

    static void RegisterCommand(const std::string& name, const std::string& usage,
                                const std::string& description, CommandHandler handler);

    static void Execute(const std::string& commandLine);
    static void Print(const std::string& message);

    struct CommandEntry {
        std::string name;
        std::string usage;
        std::string description;
        CommandHandler handler;
    };

    static std::vector<CommandEntry> s_Commands;

private:
    static void InputThreadFunc();

    static std::vector<std::string> s_PendingCommands;
    static std::mutex s_QueueMutex;
    static std::thread s_InputThread;
    static bool s_StopRequested;
    static bool s_Initialized;
};

} // namespace neurender
