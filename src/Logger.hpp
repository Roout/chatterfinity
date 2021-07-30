#pragma once

#include <fstream>
#include <chrono>
#include <mutex>

enum class LogType {
    kError,
    kWarning,
    kInfo
};

// The most simple logging mechanism
class Log final { 
public:

    Log(const char* filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        os_.open(filename, std::ofstream::out);
    }

    ~Log() {
        std::lock_guard<std::mutex> lock(mutex_);
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

        std::lock_guard<std::mutex> lock(mutex_);
        os_ << ms.count() << ' ';
        switch (type) {
            case LogType::kInfo: {
                os_ << "--info: ";
            } break;
            case LogType::kError: {
                os_ << "--error: ";
            } break;
            case LogType::kWarning: {
                os_ << "--warning: ";
            } break;
            default: break;
        }
        ((os_ << " " << std::forward<Args>(args)), ...);
    }

private:
    std::ofstream   os_ {};
    std::mutex      mutex_;
};
