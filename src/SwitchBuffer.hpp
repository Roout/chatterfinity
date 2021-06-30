#pragma once

#include <string>
#include <array>
#include <vector>

#include <boost/asio.hpp>

class SwitchBuffer {
public:
    SwitchBuffer(size_t reserved = 10) {
        m_buffers[0].reserve(reserved);
        m_buffers[1].reserve(reserved);
        m_bufferSequence.reserve(reserved);
    }

    /**
     * Queue data to passibe buffer. 
     */
    void Enque(std::string&& data) {
        m_buffers[m_activeBuffer ^ 1].emplace_back(std::move(data));
    }

    /**
     * Swap buffers and update @m_bufferSequence.
     */
    void SwapBuffers() {
        m_bufferSequence.clear();

        m_buffers[m_activeBuffer].clear();
        m_activeBuffer ^= 1;

        for (const auto& buf: m_buffers[m_activeBuffer]) {
            m_bufferSequence.emplace_back(boost::asio::const_buffer(buf.c_str(), buf.size()));
        }
    }

    size_t GetQueueSize() const noexcept {
        return m_buffers[m_activeBuffer ^ 1].size();
    } 

    const std::vector<boost::asio::const_buffer>& GetBufferSequence() const noexcept {
        return m_bufferSequence;
    }

private:

    using DoubleBuffer = std::array<std::vector<std::string>, 2>;

    /**
     * Represent two sequences of some buffers
     * One sequence is active, another one is passive. 
     * They can be swapped when needed. 
     **/    
    DoubleBuffer m_buffers;

    /**
     * View const buffer sequence used by write operations. 
     */
    std::vector<boost::asio::const_buffer> m_bufferSequence;
    
    std::size_t m_activeBuffer { 0 };
};