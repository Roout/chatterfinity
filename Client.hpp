#pragma once

#include <memory>
#include <cstddef>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

class Log;
class Connection;

class Client : 
    public std::enable_shared_from_this<Client> {
public:

    Client(std::shared_ptr<boost::asio::io_context> context);

    ~Client();

    void Run();

    void Reset();

private:

    void Connect(const char *address, int port);

private:
    std::shared_ptr<Log> m_log { nullptr };
    std::shared_ptr<boost::asio::io_context> m_context { nullptr };
    std::shared_ptr<boost::asio::ssl::context> m_sslContext { nullptr };
    std::shared_ptr<Connection> m_connection { nullptr };
    std::size_t id { 0 };
};
