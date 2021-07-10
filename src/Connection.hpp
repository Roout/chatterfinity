#pragma once

#include <string_view>
#include <string>
#include <memory>
#include <functional>

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
    ~Connection();

    void Close();

    void Write(std::string text, std::function<void()> onSuccess = {});

    net::http::Message AcquireResponse() noexcept;

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

    // TODO: temporary stuff; used while the exception system/error handling is not implemented
    // Just `post` `Connection::Close` through `strand`
    void InitiateSocketShutdown();

private:
    struct Chunk {
        // size of chunk
        size_t size_ { 0 };
        // number of consumed (processed) chunks
        size_t consumed_ { 0 }; 

        void Reset() noexcept {
            size_ = consumed_ = 0;
        }
    };

    static constexpr std::string_view kService { "https" };
    static constexpr std::string_view kCRLF { "\r\n" };
    static constexpr std::string_view kHeaderDelimiter { "\r\n\r\n" };

    // === Boost IO stuff ===
    io_context_pointer context_ { nullptr };
    ssl_context_pointer sslContext_ { nullptr };
    tcp::resolver resolver_;
    boost::asio::io_context::strand strand_;
    ssl::stream<tcp::socket> socket_;
    
    // === Connection details === 
    const size_t id_ { 0 };
    const std::string host_ {};
    std::shared_ptr<Log> log_ { nullptr };
    std::function<void()> onSuccess_;

    // === Read ===
    boost::asio::streambuf inbox_;
    net::http::Header header_;
    Chunk chunk_;
    net::http::Body body_;

    // === Write ===
    std::string outbox_;
};
