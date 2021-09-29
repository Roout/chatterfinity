#pragma once
#include <chrono>
#include <unordered_map>
#include <string>
#include <any>
#include <mutex>

namespace chrono = std::chrono;

class CacheSlot {
public:
    using TimePoint = chrono::time_point<chrono::steady_clock>;
    using Duration = chrono::seconds;

    CacheSlot() 
        : content_ {}
        , update_ { chrono::steady_clock::now() }
        , duration_ { 0 }
    {}

    template<typename T>
    CacheSlot(T&& value, Duration /* in secs */ lifetime) 
        : content_ { std::move(value) }
        , update_ { chrono::steady_clock::now() }
        , duration_ { lifetime }
    {}

    template<typename T>
    void Insert(T&& value, Duration /* in secs */ lifetime) {
        std::lock_guard<std::mutex> lock{ mutex_ };
        content_.emplace<T>(std::forward<T>(value));
        update_ = chrono::steady_clock::now();
        duration_ = lifetime;
    }

    bool IsValid() const {
        const auto now = chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock{ mutex_ };
        return chrono::duration_cast<Duration>(now - update_) < duration_;
    }

    template<typename T>
    const T* Get() const {
        std::lock_guard<std::mutex> lock{ mutex_ };
        return std::any_cast<T>(&content_);
    }

private:
    std::any content_ {};
    TimePoint update_ { chrono::steady_clock::now() };
    Duration duration_ { 0 };
    mutable std::mutex mutex_;
};
