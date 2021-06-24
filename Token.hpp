#pragma once
#include <chrono>
#include <string>
#include <mutex>

namespace chrono = std::chrono;

class Token {
public:
    using TimePoint_t = chrono::time_point<chrono::steady_clock>;
    using Duration_t = chrono::seconds;

    Token() 
        : content_ {}
        , update_ { chrono::steady_clock::now() }
        , duration_ { 0 }
    {}

    Token(std::string token, Duration_t expireTime) 
        : content_ { std::move(token) }
        , update_ { chrono::steady_clock::now() }
        , duration_ { expireTime }
    {}

    void Emplace(std::string token, Duration_t expireTime) noexcept {
        std::lock_guard<std::mutex> lock{ mutex_ };
        content_ = std::move(token);
        update_ = chrono::steady_clock::now();
        duration_ = expireTime;
    }

    bool IsValid() const noexcept {
        const auto now = chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock{ mutex_ };
        return chrono::duration_cast<Duration_t>(now - update_) < duration_;
    }

    std::string Get() const {
        std::lock_guard<std::mutex> lock{ mutex_ };
        return content_;
    }

private:

    std::string content_ {};
    TimePoint_t update_ { chrono::steady_clock::now() };
    Duration_t  duration_ { 0 };
    mutable std::mutex  mutex_;
};