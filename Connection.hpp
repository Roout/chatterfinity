#pragma once

#include <string_view>
#include <string>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Logger.hpp"
#include "SwitchBuffer.hpp"

class Client;

class Connection : 
    public std::enable_shared_from_this<Connection> 
{
public:
    Connection(std::size_t id
        , std::weak_ptr<Client> client
        , std::shared_ptr<Log> log
        , std::shared_ptr<boost::asio::io_context> context
        , std::shared_ptr<boost::asio::ssl::context>
    );

    void Start(const char *address, const char *protocol);

    // not thread-safe
    void Write(std::string text);

    void Read();

private:

    void WriteBuffer();

    void OnConnect(const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint);

    void OnWrite(const boost::system::error_code& error, std::size_t bytes);

    void OnRead(const boost::system::error_code& error, std::size_t bytes);

    void Shutdown();

    void Handshake();

private:
    const std::string_view DELIMITER { "\r\n\r\n" };

    const std::size_t m_id { 0 };
    std::shared_ptr<Log> m_log { nullptr };
    std::weak_ptr<Client> m_client;

    std::shared_ptr<boost::asio::io_context> m_context { nullptr };
    std::shared_ptr<boost::asio::ssl::context> m_sslContext { nullptr };
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_socket;
    boost::asio::io_context::strand m_strand;
    
    boost::asio::streambuf m_inbox;
    SwitchBuffer m_outbox;
    bool m_isIdle { true };
};
