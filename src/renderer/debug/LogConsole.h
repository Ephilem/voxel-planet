#pragma once

#include "core/log/Logger.h"
#include <flecs.h>
#include <vector>
#include <string>
#include <mutex>

#include "IImGuiDebugModule.h"

class LogConsole : public IImGuiDebugModule {
public:
    LogConsole();
    ~LogConsole() override;

    void draw();

    void clear();

    void setMaxEntries(size_t max) { maxEntries = max; }
    void setAutoScroll(bool enable) { autoScroll = enable; }

    void register_ecs(flecs::world &ecs) override;

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
