#pragma once

#include <string_view>
#include <string>
#include <memory>
#include <optional>
#include <functional>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Logger.hpp"
#include "Response.hpp"
#include "SwitchBuffer.hpp"

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

class Connection 
    : public std::enable_shared_from_this<Connection> 
{
public:
    using SharedIOContext = std::shared_ptr<boost::asio::io_context>;
    using SharedSSLContext = std::shared_ptr<boost::asio::ssl::context>;
    using Stream = ssl::stream<tcp::socket>;

    Connection(SharedIOContext
        , SharedSSLContext
        , std::string_view host
        , std::string_view service
        , size_t id
    );

    virtual ~Connection();

    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;
    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;

    void ScheduleShutdown();

    void Connect(std::function<void()> onConnect = {});

    void ScheduleWrite(std::string text, std::function<void()> onWrite = {});

    virtual void Read(std::function<void()> onRead = {}) = 0;

protected:

    // NOTE: Can not be called outside because there will be a data race at least around `socket_`.
    // Posting it through `strand_` gurantees that no other handler is being executed in other thread so
    // it's safe to invoke `Close`
    void Close();

    // repeat the same actions via `onConnectSuccess_` callback 
    // on successfull reconnection
    void Reconnect();

    void Write();

    void OnResolve(const boost::system::error_code&, tcp::resolver::results_type);

    void OnConnect(const boost::system::error_code&, const tcp::endpoint&);

    void OnHandshake(const boost::system::error_code&);

    void OnWrite(const boost::system::error_code&, size_t);

    void OnTimeout(const boost::system::error_code&);

protected:
    // === Boost IO stuff ===
    SharedIOContext context_ { nullptr };
    SharedSSLContext ssl_ { nullptr };
    tcp::resolver resolver_;
    boost::asio::io_context::strand strand_;
    std::optional<Stream> socket_;
    boost::asio::deadline_timer timer_;

    const std::string host_;
    const std::string service_;    
    std::shared_ptr<Log> log_ { nullptr };

    // === callbacks ===
    std::function<void()> onConnectSuccess_;
    std::function<void()> onWriteSuccess_;
    std::function<void()> onReadSuccess_;

    // === Write ===
    SwitchBuffer outbox_;
    bool isWriting_ { false };

    // === Reconnect ===
    static constexpr size_t kReconnectLimit { 3 };
    size_t reconnects_ { 0 };
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