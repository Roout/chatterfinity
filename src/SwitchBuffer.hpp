#pragma once
#include <string>
#include <array>
#include <vector>
#include <cassert>

#include <boost/asio.hpp>

class SwitchBuffer {
public:
    SwitchBuffer(size_t reserved = 10) {
        for (auto& buffer: buffers_) {
            buffer.reserve(reserved);
        }
        bufferSequence_.reserve(reserved);
    }

    /**
     * Queue data to passibe buffer. 
     */
    void Enque(std::string data) {
        assert(activeBuffer_ < buffers_.size());
        buffers_[activeBuffer_ ^ 1].emplace_back(std::move(data));
    }

    /**
     * Swap buffers and update @bufferSequence_.
     */
    void SwapBuffers() {
        assert(activeBuffer_ < buffers_.size());

        bufferSequence_.clear();
        buffers_[activeBuffer_].clear();
        activeBuffer_ ^= 1;

        for (const auto& buf: buffers_[activeBuffer_]) {
            bufferSequence_.emplace_back(boost::asio::const_buffer(buf.c_str(), buf.size()));
        }
    }

    size_t GetQueueSize() const noexcept {
        assert(activeBuffer_ < buffers_.size());
        return buffers_[activeBuffer_ ^ 1].size();
    } 

    const std::vector<boost::asio::const_buffer>& GetBufferSequence() const noexcept {
        return bufferSequence_;
    }

private:

    using DoubleBuffer = std::array<std::vector<std::string>, 2>;

    /**
     * Represent two sequences of some buffers
     * One sequence is active, another one is passive. 
     * They can be swapped when needed. 
     **/    
    DoubleBuffer buffers_;

    /**
     * View const buffer sequence used by write operations. 
     */
    std::vector<boost::asio::const_buffer> bufferSequence_;
    
    size_t activeBuffer_ { 0 };
};