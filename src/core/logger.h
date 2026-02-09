#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace celegans {

enum class LogLevel : int {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    NONE = 4
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) { level_ = level; }

    void set_file(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.open(path, std::ios::out | std::ios::trunc);
    }

    template <typename... Args>
    void log(LogLevel level, const char* file, int line, Args&&... args) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);

        std::ostringstream oss;
        oss << "[" << level_str(level) << "] ";
        oss << timestamp() << " ";
        oss << file << ":" << line << " | ";
        (oss << ... << std::forward<Args>(args));
        oss << "\n";

        std::string msg = oss.str();
        std::cerr << msg;
        if (file_.is_open()) {
            file_ << msg;
            file_.flush();
        }
    }

private:
    Logger() = default;
    LogLevel level_ = LogLevel::INFO;
    std::ofstream file_;
    std::mutex mutex_;

    static const char* level_str(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default: return "?????";
        }
    }

    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
};

#define LOG_DEBUG(...) celegans::Logger::instance().log(celegans::LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  celegans::Logger::instance().log(celegans::LogLevel::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  celegans::Logger::instance().log(celegans::LogLevel::WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) celegans::Logger::instance().log(celegans::LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)

} // namespace celegans
