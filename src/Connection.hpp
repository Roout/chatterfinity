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
    using SharedIOContext = std::shared_ptr<boost::asio::io_context>;
    using SharedSSLContext = std::shared_ptr<boost::asio::ssl::context>;

    Connection(SharedIOContext, SharedSSLContext, const std::string& log);

    virtual ~Connection();

    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;

    void Close();

    void Connect(std::string_view host
        , std::string_view service
        , std::function<void()> onConnect = {});

    void Write(std::string text, std::function<void()> onWrite = {});

    virtual void Read(std::function<void()> onRead = {}) = 0;

protected:

    void OnResolve(const boost::system::error_code& error, tcp::resolver::results_type results);

    void OnConnect(const boost::system::error_code& error, const tcp::endpoint& endpoint);

    void OnHandshake(const boost::system::error_code& error);

    void OnWrite(const boost::system::error_code& error, size_t bytes);

    // TODO: temporary stuff; used while the exception system/error handling is not implemented
    // Just `post` `Connection::Close` through `strand`
    void InitiateSocketShutdown();

protected:
    // === Boost IO stuff ===
    SharedIOContext context_ { nullptr };
    SharedSSLContext ssl_ { nullptr };
    tcp::resolver resolver_;
    boost::asio::io_context::strand strand_;
    ssl::stream<tcp::socket> socket_;
    
    std::shared_ptr<Log> log_ { nullptr };
    std::function<void()> onConnectSuccess_;
    std::function<void()> onWriteSuccess_;
    std::function<void()> onReadSuccess_;

    // === Write ===
    std::string outbox_;
};

class HttpConnection: public Connection {
public:

    using Connection::Connection;
    
    void Read(std::function<void()> onSuccess = {}) override;

    net::http::Message AcquireResponse() noexcept {
        return { std::move(header_), std::move(body_) };
    }
private:
    void ReadHeader();

    void OnHeaderRead(const boost::system::error_code& error, size_t bytes);

    void ReadIntactBody();

    void OnReadIntactBody(const boost::system::error_code& error, size_t bytes);

    void ReadChunkedBody();

    void OnReadChunkedBody(const boost::system::error_code& error, size_t bytes);

private:
    struct Chunk final {
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

    // buffers
    boost::asio::streambuf inbox_;
    Chunk chunk_;
    net::http::Header header_;
    net::http::Body body_;
};

class IrcConnection: public Connection {
public:

    using Connection::Connection;

    void Read(std::function<void()> onSuccess = {}) override;

    net::irc::Message AcquireResponse() noexcept {
        return std::move(message_);
    }
private:
    void OnRead(const boost::system::error_code& error, size_t bytes);

    static constexpr std::string_view kCRLF { "\r\n" };
 
    boost::asio::streambuf inbox_;
    net::irc::Message message_;
};

namespace utils {
    template <typename Derived, 
        typename = std::enable_if_t<std::is_base_of_v<Connection, Derived>>
    >
    inline std::shared_ptr<Derived> SharedFrom(const std::shared_ptr<Connection>& base) {
        return std::static_pointer_cast<Derived>(base);
    }

    template <typename Derived, 
        typename = std::enable_if_t<std::is_base_of_v<Connection, Derived>>
    >
    inline std::weak_ptr<Derived> WeakFrom(const std::shared_ptr<Connection>& base) {
        return std::static_pointer_cast<Derived>(base);
    }
}