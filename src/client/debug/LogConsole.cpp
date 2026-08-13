#include "LogConsole.h"

#include <chrono>
#include <imgui.h>
#include <iomanip>
#include <sstream>

#include "core/TracyIntegration.h"

LogConsole::LogConsole() {
    m_sinkId = vp::Logger::addSink([this](const vp::Logger::LogEntry& entry) { on_log_received(entry); });
}

LogConsole::~LogConsole() {
    vp::Logger::removeSink(m_sinkId);
}

void LogConsole::on_log_received(const vp::Logger::LogEntry& entry) {
    std::lock_guard<std::mutex> lock(m_entriesMutex);

    auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(entry.timestamp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    m_entries.push_back({entry.level, entry.component, entry.message, ss.str()});

    if (m_entries.size() > m_maxEntries) {
        m_entries.erase(m_entries.begin(), m_entries.begin() + (m_entries.size() - m_maxEntries));
    }

    m_scrollToBottom = m_autoScroll;
}

void LogConsole::clear() {
    std::lock_guard<std::mutex> lock(m_entriesMutex);
    m_entries.clear();
}

void LogConsole::render(flecs::world&) {
    VOXEL_ZONE_N("LogConsole-Draw");
    draw();
}

void LogConsole::draw() {
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Button("Clear"))
        clear();
    ImGui::SameLine();

    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();

    const char* levels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##Level", &m_filterLevel, levels, IM_ARRAYSIZE(levels));
    ImGui::SameLine();

    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##Filter", "Filter...", m_filterText, IM_ARRAYSIZE(m_filterText));

    ImGui::Separator();

    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::lock_guard<std::mutex> lock(m_entriesMutex);

    for (const auto& entry : m_entries) {
        if (static_cast<int>(entry.level) < m_filterLevel)
            continue;

        if (m_filterText[0] != '\0') {
            if (entry.message.find(m_filterText) == std::string::npos &&
                entry.component.find(m_filterText) == std::string::npos) {
                continue;
            }
        }

        ImVec4 color;
        switch (entry.level) {
        case vp::Logger::Level::TRACE_LEVEL:
            color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            break;
        case vp::Logger::Level::DEBUG_LEVEL:
            color = ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
            break;
        case vp::Logger::Level::INFO_LEVEL:
            color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            break;
        case vp::Logger::Level::WARNING_LEVEL:
            color = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
            break;
        case vp::Logger::Level::ERROR_LEVEL:
            color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            break;
        case vp::Logger::Level::FATAL_LEVEL:
            color = ImVec4(1.0f, 0.3f, 1.0f, 1.0f);
            break;
        }

        ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color, "%-5s", vp::Logger::levelToString(entry.level));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%-12s", entry.component.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(entry.message.c_str());
    }

    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }

    ImGui::EndChild();
}
