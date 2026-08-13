#pragma once

#include "core/log/Logger.h"
#include "IDebugPanel.h"
#include <mutex>
#include <string>
#include <vector>

class LogConsole : public IDebugPanel {
public:
    LogConsole();
    ~LogConsole() override;

    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Console"; }

    const std::string category() const override { return "Logging"; }

    void clear();

    void set_max_entries(size_t max) { m_maxEntries = max; }

    void set_auto_scroll(bool enable) { m_autoScroll = enable; }

private:
    struct DisplayEntry {
        vp::Logger::Level level;
        std::string component;
        std::string message;
        std::string timestamp;
    };

    void on_log_received(const vp::Logger::LogEntry& entry);
    void draw();

    std::vector<DisplayEntry> m_entries;
    std::mutex m_entriesMutex;
    vp::Logger::SinkId m_sinkId = 0;

    size_t m_maxEntries = 1000;
    bool m_autoScroll = true;
    bool m_scrollToBottom = false;

    int m_filterLevel = 0;
    char m_filterText[128] = "";
};
