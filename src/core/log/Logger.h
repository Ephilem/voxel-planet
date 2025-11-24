#pragma once

#include <string>
#include <sstream>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>
#include <fmt/format.h>

class Logger {
public:
    enum class Level {
        TRACE,
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL,
    };

    // Log entry passed to all sinks
    struct LogEntry {
        Level level;
        std::string component;
        std::string message;
        std::chrono::system_clock::time_point timestamp;
    };

    // Sink: receives log entries (multiple sinks can be registered)
    using SinkId = size_t;
    using SinkCallback = std::function<void(const LogEntry&)>;

    struct Colors {
        static constexpr const char* RED     = "\033[1;31m";
        static constexpr const char* YELLOW  = "\033[1;33m";
        static constexpr const char* GREEN   = "\033[1;32m";
        static constexpr const char* BLUE    = "\033[1;34m";
        static constexpr const char* MAGENTA = "\033[1;35m";
        static constexpr const char* CYAN    = "\033[1;36m";
        static constexpr const char* GRAY    = "\033[1;90m";
        static constexpr const char* RESET   = "\033[0m";
    };

    explicit Logger(std::string component);

    void trace(const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

    template<typename... Args>
    void trace(fmt::format_string<Args...> format, Args&&... args) {
        trace(fmt::format(format, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void debug(fmt::format_string<Args...> format, Args&&... args) {
        debug(fmt::format(format, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void info(fmt::format_string<Args...> format, Args&&... args) {
        info(fmt::format(format, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void warning(fmt::format_string<Args...> format, Args&&... args) {
        warning(fmt::format(format, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void error(fmt::format_string<Args...> format, Args&&... args) {
        error(fmt::format(format, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void fatal(fmt::format_string<Args...> format, Args&&... args) {
        fatal(fmt::format(format, std::forward<Args>(args)...));
    }

    void setLogLevel(Level level) { m_minimumLevel = level; }
    void setUseColors(bool use) { m_useColors = use; }
    void setShowTimestamp(bool show) { m_showTimestamp = show; }

    const std::string& getComponent() const { return m_componentName; }

    // Static access,get or create a logger for a component
    static Logger& get(const std::string& component);

    // Global settings
    static void setGlobalLevel(Level level);
    static void setGlobalColors(bool use);
    static void setGlobalTimestamp(bool show);

    // Sink management
    static SinkId addSink(SinkCallback callback);
    static void removeSink(SinkId id);
    static void clearSinks();

    // Utility
    static const char* levelToString(Level level);
    static const char* levelToColor(Level level);

private:
    void log(Level level, const std::string& message);
    static void dispatchToSinks(const LogEntry& entry);

    static std::map<std::string, size_t> maxLengths;
    static std::map<std::string, std::unique_ptr<Logger>> loggers;
    static std::map<SinkId, SinkCallback> sinks;
    static SinkId nextSinkId;
    static Level globalMinLevel;
    static bool globalUseColors;
    static bool globalShowTimestamp;

    static std::string getCurrentTime();
    static std::string formatMessage(const std::string& message);

    const std::string m_componentName;
    Level m_minimumLevel;
    bool m_useColors;
    bool m_showTimestamp;
};

// Macros for easy logging anywhere
// Usage: LOG_INFO("MyComponent", "Player {} joined", playerName);

#define LOG_TRACE(component, ...) ::Logger::get(component).trace(__VA_ARGS__)
#define LOG_DEBUG(component, ...) ::Logger::get(component).debug(__VA_ARGS__)
#define LOG_INFO(component, ...)  ::Logger::get(component).info(__VA_ARGS__)
#define LOG_WARN(component, ...)  ::Logger::get(component).warning(__VA_ARGS__)
#define LOG_ERROR(component, ...) ::Logger::get(component).error(__VA_ARGS__)
#define LOG_FATAL(component, ...) ::Logger::get(component).fatal(__VA_ARGS__)
