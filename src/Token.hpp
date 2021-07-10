#pragma once
#include <chrono>
#include <string>
#include <mutex>

namespace chrono = std::chrono;

class Token {
public:
    using TimePoint = chrono::time_point<chrono::steady_clock>;
    using Duration = chrono::seconds;

    Token() 
        : content_ {}
        , update_ { chrono::steady_clock::now() }
        , duration_ { 0 }
    {}

    Token(std::string token, Duration expireTime) 
        : content_ { std::move(token) }
        , update_ { chrono::steady_clock::now() }
        , duration_ { expireTime }
    {}

    void Emplace(std::string token, Duration expireTime) noexcept {
        std::lock_guard<std::mutex> lock{ mutex_ };
        content_ = std::move(token);
        update_ = chrono::steady_clock::now();
        duration_ = expireTime;
    }

    bool IsValid() const noexcept {
        const auto now = chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock{ mutex_ };
        return chrono::duration_cast<Duration>(now - update_) < duration_;
    }

    std::string Get() const {
        std::lock_guard<std::mutex> lock{ mutex_ };
        return content_;
    }

private:

    std::string content_ {};
    TimePoint update_ { chrono::steady_clock::now() };
    Duration  duration_ { 0 };
    mutable std::mutex  mutex_;
};