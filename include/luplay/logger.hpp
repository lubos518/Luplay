#ifndef LUPLAY_LOGGER_HPP
#define LUPLAY_LOGGER_HPP

#include <string>
#include <vector>
#include <mutex>
#include <deque>

namespace luplay {

enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogEntry {
    LogLevel level;
    std::string timestamp;
    std::string message;
};

class Logger {
public:
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    static std::vector<LogEntry> get_logs();
    static void clear();

private:
    static void log(LogLevel level, const std::string& message);

    static std::mutex mutex_;
    static std::deque<LogEntry> logs_;
    static const size_t MAX_LOGS = 1000;
};

} // namespace luplay

#endif // LUPLAY_LOGGER_HPP
