#include "Logger.h"

#include <iostream>
#include <chrono>
#include <iomanip>

// Static member initialization
std::map<std::string, size_t> Logger::maxLengths = {
    {"LEVEL", 5},
    {"COMPONENT", 20}
};

std::map<std::string, std::unique_ptr<Logger>> Logger::loggers;
std::map<Logger::SinkId, Logger::SinkCallback> Logger::sinks;
Logger::SinkId Logger::nextSinkId = 0;
Logger::Level Logger::globalMinLevel = Logger::Level::TRACE;
bool Logger::globalUseColors = true;
bool Logger::globalShowTimestamp = true;

Logger::Logger(std::string component)
    : m_componentName(std::move(component))
    , m_minimumLevel(globalMinLevel)
    , m_useColors(globalUseColors)
    , m_showTimestamp(globalShowTimestamp) {
    maxLengths["COMPONENT"] = std::max(maxLengths["COMPONENT"], m_componentName.size());
}

Logger& Logger::get(const std::string& component) {
    auto it = loggers.find(component);
    if (it == loggers.end()) {
        auto [inserted, _] = loggers.emplace(component, std::make_unique<Logger>(component));
        return *inserted->second;
    }
    return *it->second;
}

void Logger::setGlobalLevel(Level level) {
    globalMinLevel = level;
    for (auto& [name, logger] : loggers) {
        logger->setLogLevel(level);
    }
}

void Logger::setGlobalColors(bool use) {
    globalUseColors = use;
    for (auto& [name, logger] : loggers) {
        logger->setUseColors(use);
    }
}

void Logger::setGlobalTimestamp(bool show) {
    globalShowTimestamp = show;
    for (auto& [name, logger] : loggers) {
        logger->setShowTimestamp(show);
    }
}

Logger::SinkId Logger::addSink(SinkCallback callback) {
    SinkId id = nextSinkId++;
    sinks[id] = std::move(callback);
    return id;
}

void Logger::removeSink(SinkId id) {
    sinks.erase(id);
}

void Logger::clearSinks() {
    sinks.clear();
}

void Logger::dispatchToSinks(const LogEntry& entry) {
    for (const auto& [id, callback] : sinks) {
        callback(entry);
    }
}

void Logger::log(Level level, const std::string& message) {
    if (level < m_minimumLevel) return;

    auto now = std::chrono::system_clock::now();

    // Dispatch to all registered sinks
    LogEntry entry{level, m_componentName, message, now};
    dispatchToSinks(entry);

    // Console output (built-in sink)
    std::stringstream ss;

    if (m_showTimestamp) {
        ss << Logger::Colors::GRAY << "[" << getCurrentTime() << "] " << Logger::Colors::RESET;
    }

    const char* levelStr = levelToString(level);
    if (m_useColors) {
        ss << levelToColor(level);
    }
    ss << std::left << std::setw(static_cast<int>(maxLengths["LEVEL"])) << levelStr;
    if (m_useColors) {
        ss << Colors::RESET;
    }

    ss << " " << Colors::CYAN
       << std::left << std::setw(static_cast<int>(maxLengths["COMPONENT"])) << m_componentName
       << Colors::RESET;

    ss << " " << formatMessage(message);

    if (level >= Level::ERROR) {
        std::cerr << ss.str();
    } else {
        std::cout << ss.str();
    }
}

void Logger::trace(const std::string& message) { log(Level::TRACE, message); }
void Logger::debug(const std::string& message) { log(Level::DEBUG, message); }
void Logger::info(const std::string& message) { log(Level::INFO, message); }
void Logger::warning(const std::string& message) { log(Level::WARNING, message); }
void Logger::error(const std::string& message) { log(Level::ERROR, message); }
void Logger::fatal(const std::string& message) { log(Level::FATAL, message); }

const char* Logger::levelToString(Level level) {
    switch (level) {
        case Level::TRACE:   return "TRACE";
        case Level::DEBUG:   return "DEBUG";
        case Level::INFO:    return "INFO";
        case Level::WARNING: return "WARN";
        case Level::ERROR:   return "ERROR";
        case Level::FATAL:   return "FATAL";
        default:             return "?????";
    }
}

const char* Logger::levelToColor(Level level) {
    switch (level) {
        case Level::TRACE:   return Colors::GRAY;
        case Level::DEBUG:   return Colors::BLUE;
        case Level::INFO:    return Colors::GREEN;
        case Level::WARNING: return Colors::YELLOW;
        case Level::ERROR:   return Colors::RED;
        case Level::FATAL:   return Colors::MAGENTA;
        default:             return Colors::RESET;
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::formatMessage(const std::string& message) {
    std::stringstream formatted;
    std::istringstream stream(message);
    std::string line;
    bool firstLine = true;

    while (std::getline(stream, line)) {
        if (!firstLine) {
            formatted << "\n" << std::string(maxLengths["LEVEL"] + maxLengths["COMPONENT"] + 20, ' ');
        }
        formatted << line;
        firstLine = false;
    }
    formatted << "\n";

    return formatted.str();
}
