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

    void set_max_entries(size_t max) { m_maxEntries = max; }
    void set_auto_scroll(bool enable) { m_autoScroll = enable; }

    void register_ecs(flecs::world &ecs) override;

    const char* get_name() const override { return "Console"; }
    const char* get_category() const override { return "Logging"; }
    bool is_visible() const override { return isOpen; }
    void set_visible(bool visible) override { isOpen = visible; }

    bool isOpen = false;

private:
    struct DisplayEntry {
        Logger::Level level;
        std::string component;
        std::string message;
        std::string timestamp;
    };

    void on_log_received(const Logger::LogEntry& entry);

    std::vector<DisplayEntry> m_entries;
    std::mutex m_entriesMutex;
    Logger::SinkId m_sinkId = 0;

    size_t m_maxEntries = 1000;
    bool m_autoScroll = true;
    bool m_scrollToBottom = false;

    // Filters
    int m_filterLevel = 0;  // Index into level names
    char m_filterText[128] = "";
};
