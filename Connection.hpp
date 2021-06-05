#pragma once

#include <string_view>
#include <string>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Logger.hpp"
#include "Response.hpp"

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

class Connection : 
    public std::enable_shared_from_this<Connection> 
{
public:
    using io_context_pointer = std::shared_ptr<boost::asio::io_context>;
    using ssl_context_pointer = std::shared_ptr<boost::asio::ssl::context>;

    Connection(io_context_pointer
        , ssl_context_pointer
        , size_t id
        , std::string_view host
    );

    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    ~Connection() { Close(); }

    void Close();

    void Write(std::string text);

private:

    void ReadHeader();

    void WriteBuffer();

    void OnResolve(const boost::system::error_code& error, tcp::resolver::results_type results);

    void OnConnect(const boost::system::error_code& error, const tcp::endpoint& endpoint);

    void OnHandshake(const boost::system::error_code& error);

    void OnWrite(const boost::system::error_code& error, size_t bytes);

    void OnHeaderRead(const boost::system::error_code& error, size_t bytes);

    void ReadIntactBody();

    void OnReadIntactBody(const boost::system::error_code& error, size_t bytes);

    void ReadChunkedBody();

    void OnReadChunkedBody(const boost::system::error_code& error, size_t bytes);

private:
    struct Chunk {
        // size of chunk
        size_t m_size { 0 };
        // number of consumed (processed) chunks
        size_t m_consumed { 0 }; 

        void Reset() noexcept {
            m_size = m_consumed = 0;
        }
    };

    static constexpr std::string_view kService { "https" };
    static constexpr std::string_view kCRLF { "\r\n" };
    static constexpr std::string_view kHeaderDelimiter { "\r\n\r\n" };

    io_context_pointer m_context { nullptr };
    ssl_context_pointer m_sslContext { nullptr };
    tcp::resolver m_resolver;
    boost::asio::io_context::strand m_strand;
    ssl::stream<tcp::socket> m_socket;
    
    const size_t m_id { 0 };
    const std::string m_host {};
    std::shared_ptr<Log> m_log { nullptr };

    boost::asio::streambuf m_inbox;
    blizzard::Header m_header;
    Chunk m_chunk;
    std::string m_body;

    std::string m_outbox;
};
