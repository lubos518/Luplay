#include "luplay/logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace luplay {

std::mutex Logger::mutex_;
std::deque<LogEntry> Logger::logs_;

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
    std::cout << message << std::endl;
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
    std::cerr << "[Warning] " << message << std::endl;
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
    std::cerr << "[Error] " << message << std::endl;
}

std::vector<LogEntry> Logger::get_logs() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<LogEntry>(logs_.begin(), logs_.end());
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.clear();
}

void Logger::log(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &in_time_t);
#else
    localtime_r(&in_time_t, &tm_buf);
#endif
    ss << std::put_time(&tm_buf, "%H:%M:%S");

    std::lock_guard<std::mutex> lock(mutex_);
    logs_.push_back({level, ss.str(), message});
    
    if (logs_.size() > MAX_LOGS) {
        logs_.pop_front();
    }
}

} // namespace luplay
