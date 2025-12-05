#include "LogConsole.h"
#include "renderer/Renderer.h"

#include <imgui.h>
#include <chrono>
#include <iomanip>
#include <sstream>

LogConsole::LogConsole() {
    // Register as a sink to receive log messages
    m_sinkId = Logger::addSink([this](const Logger::LogEntry& entry) {
        on_log_received(entry);
    });
}

LogConsole::~LogConsole() {
    Logger::removeSink(m_sinkId);
}

void LogConsole::on_log_received(const Logger::LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_entriesMutex);

    // Format timestamp
    auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    m_entries.push_back({
        entry.level,
        entry.component,
        entry.message,
        ss.str()
    });

    // Trim old entries
    if (m_entries.size() > m_maxEntries) {
        m_entries.erase(m_entries.begin(), m_entries.begin() + (m_entries.size() - m_maxEntries));
    }

    m_scrollToBottom = m_autoScroll;
}

void LogConsole::clear() {
    std::lock_guard<std::mutex> lock(m_entriesMutex);
    m_entries.clear();
}

void LogConsole::register_ecs(flecs::world &ecs) {
    ecs.system<Renderer>("ImGuiDebugLogConsole")
        .kind(flecs::PreStore)
        .each([this](flecs::entity e, Renderer& renderer) {
            if (renderer.debugModuleManager) {
                this->draw();
            }
        });
}

void LogConsole::draw() {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log Console", &isOpen)) {
        ImGui::End();
        return;
    }

    // Toolbar
    if (ImGui::Button("Clear")) {
        clear();
    }
    ImGui::SameLine();

    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();

    // Level filter
    const char* levels[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##Level", &m_filterLevel, levels, IM_ARRAYSIZE(levels));
    ImGui::SameLine();

    // Text filter
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##Filter", "Filter...", m_filterText, IM_ARRAYSIZE(m_filterText));

    ImGui::Separator();

    // Log entries
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::lock_guard<std::mutex> lock(m_entriesMutex);

    for (const auto& entry : m_entries) {
        // Filter by level
        if (static_cast<int>(entry.level) < m_filterLevel) continue;

        // Filter by text
        if (m_filterText[0] != '\0') {
            if (entry.message.find(m_filterText) == std::string::npos &&
                entry.component.find(m_filterText) == std::string::npos) {
                continue;
            }
        }

        // Color based on level
        ImVec4 color;
        switch (entry.level) {
            case Logger::Level::TRACE:   color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
            case Logger::Level::DEBUG:   color = ImVec4(0.4f, 0.6f, 1.0f, 1.0f); break;
            case Logger::Level::INFO:    color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break;
            case Logger::Level::WARNING: color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f); break;
            case Logger::Level::ERROR:   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
            case Logger::Level::FATAL:   color = ImVec4(1.0f, 0.3f, 1.0f, 1.0f); break;
        }

        // Timestamp (gray)
        ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
        ImGui::SameLine();

        // Level (colored)
        ImGui::TextColored(color, "%-5s", Logger::levelToString(entry.level));
        ImGui::SameLine();

        // Component (cyan)
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%-12s", entry.component.c_str());
        ImGui::SameLine();

        // Message
        ImGui::TextUnformatted(entry.message.c_str());
    }

    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }

    ImGui::EndChild();
    ImGui::End();
}
