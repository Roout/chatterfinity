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
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
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
        ((ss << " " << std::forward<Args>(args)), ...);
        std::lock_guard<std::mutex> lock(mutex_);
        os_ << ss.str();
        os_.flush();
    }

private:
    std::ofstream   os_ {};
    std::mutex      mutex_;
};
