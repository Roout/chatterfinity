#pragma once

#include <fstream>
#include <chrono>
#include <mutex>
#include <sstream>

enum class LogType {
    kError,
    kWarning,
    kInfo
};

// The most simple logging mechanism
class Log final { 
public:

    Log(const char* filename) {
        os_.open(filename, std::ofstream::out);
    }

    ~Log() {
        if (os_.is_open()) {
            os_.close();
        }
    }
    
    template<class ...Args>
    void Write(const LogType type, Args &&... args) {
        const auto now = std::chrono::high_resolution_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        );

        std::stringstream ss;
        ss << ms.count() << ' ';
        switch (type) {
            case LogType::kInfo: {
                ss << "[info ] ";
            } break;
            case LogType::kError: {
                ss << "[error] ";
            } break;
            case LogType::kWarning: {
                ss << "[warn ] ";
            } break;
            default: break;
        }
        ((ss << std::forward<Args>(args)), ...);
        std::lock_guard<std::mutex> lock(mutex_);
        os_ << ss.str() << '\n';
        os_.flush();
    }

private:
    std::ofstream   os_ {};
    std::mutex      mutex_;
};

template<class ...Args>
inline void Warn(Log& log, Args&& ...args) {
    log.Write(LogType::kWarning, std::forward<Args>(args)...);
}
template<class ...Args>
inline void Info(Log& log, Args&& ...args) {
    log.Write(LogType::kInfo, std::forward<Args>(args)...);
}
template<class ...Args>
inline void Error(Log& log, Args&& ...args) {
    log.Write(LogType::kError, std::forward<Args>(args)...);
}

#define LOG_WARN(logger, ...) \
    Warn((logger), __func__, ": ", __VA_ARGS__)

#define LOG_INFO(logger, ...) \
    Info((logger), __func__, ": ", __VA_ARGS__)

#define LOG_ERROR(logger, ...) \
    Error((logger), __func__, ": ", __VA_ARGS__)


