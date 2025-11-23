#pragma once

#include "core/log/Logger.h"
#include <flecs.h>
#include <vector>
#include <string>
#include <mutex>

class LogConsole {
public:
    LogConsole();
    ~LogConsole();

    // Register ECS systems for the console
    static void Register(flecs::world& world);

    // Draw the ImGui console window
    void draw();

    // Clear all logs
    void clear();

    // Settings
    void setMaxEntries(size_t max) { maxEntries = max; }
    void setAutoScroll(bool enable) { autoScroll = enable; }

    bool isOpen = true;

private:
    struct DisplayEntry {
        Logger::Level level;
        std::string component;
        std::string message;
        std::string timestamp;
    };

    void onLogReceived(const Logger::LogEntry& entry);

    std::vector<DisplayEntry> entries;
    std::mutex entriesMutex;
    Logger::SinkId sinkId = 0;

    size_t maxEntries = 1000;
    bool autoScroll = true;
    bool scrollToBottom = false;

    // Filters
    int filterLevel = 0;  // Index into level names
    char filterText[128] = "";
};
