// Source: https://gist.github.com/Roout/c3be2d97809758c3f6936c6b238c3b3a
#include <mutex>
#include <condition_variable>
#include <array>
#include <cassert>
#include <type_traits>

template<typename T, std::size_t Capacity = 255>
class CcQueue {
public:
    static_assert(std::is_move_constructible_v<T>, "T must be move-constructible");
    static_assert(std::is_move_assignable_v<T>, "T must be move-assignable");

    static constexpr std::size_t kCapacity { Capacity };

    using element = T;
    using container = std::array<element, kCapacity>;

    CcQueue() 
        : front_ { 0 }
        , back_ { 0 }
        , size_ { 0 }
    {
        assert(front_ == back_);
    }

    // return true if value was pushed successfully
    // otherwise return false on failure and doesn't block
    [[nodiscard]] bool TryPush(element cmd) {
        bool isPushed = false;
        if (std::unique_lock<std::mutex> lock { mutex_ }; 
            !IsFull()
        ) {
            PushBack(std::move(cmd));
            lock.unlock();
            isPushed = true;
            // notify consumers
            notifier_.notify_all();
        }
        return isPushed;
    }

    // return front element if queue isn't empty
    // otherwise blocks
    [[nodiscard]] element Pop() {
        std::unique_lock<std::mutex> lock { mutex_ };  
        notifier_.wait(lock, [this]() { 
            return !IsEmpty(); 
        });
        return PopFront();
    }

private:

    void PushBack(element&& value) noexcept {
        assert(!IsFull());
        container_[back_] = std::move(value);
        back_ = (back_ + 1) % kCapacity;
        size_++;
    }

    [[nodiscard]] element PopFront() {
        assert(!IsEmpty());
        element value = std::move(container_[front_]);
        front_ = (front_ + 1) % kCapacity;
        size_--;
        return value;
    }

    [[nodiscard]] bool IsEmpty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] bool IsFull() const noexcept {
        return size_ == kCapacity;
    }

    std::mutex mutex_;
    std::condition_variable notifier_;
    container container_;
    std::size_t front_;
    std::size_t back_;
    std::size_t size_;
};
