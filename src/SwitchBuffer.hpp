#pragma once
#include <string>
#include <array>
#include <vector>
#include <cassert>
#include <functional>

#include <boost/asio.hpp>

class SwitchBuffer {
public:
    using ConstBuffer = boost::asio::const_buffer;
    using FuncRef = std::reference_wrapper<std::function<void()>>;

    SwitchBuffer(size_t reserved = 10) {
        for (auto& buffer: buffers_) {
            buffer.Reserve(reserved);
        }
        bufferSequence_.Reserve(reserved);
    }

    /**
     * Queue data to passive buffer. 
     */
    void Enque(std::string data, std::function<void()> callback) {
        assert(activeBuffer_ < buffers_.size());
        buffers_[activeBuffer_ ^ 1].Append(std::move(data), std::move(callback));
    }

    /**
     * Swap buffers and update @bufferSequence_.
     */
    void SwapBuffers() {
        assert(activeBuffer_ < buffers_.size());

        bufferSequence_.Clear();
        buffers_[activeBuffer_].Clear();
        activeBuffer_ ^= 1;

        auto& buffer = buffers_[activeBuffer_];
        for (size_t i = 0; i < buffer.Size(); i++) {
            bufferSequence_.Append(buffer.data[i], buffer.callbacks[i]);
        }
    }

    size_t GetQueueSize() const noexcept {
        assert(activeBuffer_ < buffers_.size());
        return buffers_[activeBuffer_ ^ 1].Size();
    } 

    const std::vector<ConstBuffer>& GetBufferSequence() const noexcept {
        return bufferSequence_.data;
    }

    const std::vector<FuncRef>& GetCallbackSequence() const noexcept {
        return bufferSequence_.callbacks;
    }

private:
    // TODO: get rid of code duplication
    struct BufferData {
        std::vector<std::string> data;
        std::vector<std::function<void()>> callbacks;

        void Append(std::string text, std::function<void()> callback) {
            data.push_back(std::move(text));
            callbacks.push_back(std::move(callback));
        }

        void Reserve(size_t n) {
            data.reserve(n);
            callbacks.reserve(n);
        }

        void Clear() noexcept {
            data.clear();
            callbacks.clear();
        }

        size_t Size() const noexcept {
            assert(data.size() == callbacks.size());
            return data.size();
        }
    };

    struct BufferView {
        std::vector<ConstBuffer> data;
        std::vector<FuncRef> callbacks;

        void Append(const std::string& text, std::function<void()>& callback) {
            data.emplace_back(text.data(), text.size());
            callbacks.emplace_back(std::ref(callback));
        }

        void Reserve(size_t n) {
            data.reserve(n);
            callbacks.reserve(n);
        }

        void Clear() noexcept {
            data.clear();
            callbacks.clear();
        }

        size_t Size() const noexcept {
            assert(data.size() == callbacks.size());
            return data.size();
        }
    };

    using DoubleBuffer = std::array<BufferData, 2>;

    /**
     * Represent two sequences of some buffers
     * One sequence is active, another one is passive. 
     * They can be swapped when needed. 
     **/    
    DoubleBuffer buffers_;

    /**
     * View const buffer sequence used by write operations. 
     */
    BufferView bufferSequence_;
    
    size_t activeBuffer_ { 0 };
};