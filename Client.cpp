#include "Client.hpp"
#include "Connection.hpp"

#include <vector>
#include <exception>
#include <thread>
#include <iostream>
#include <memory>

namespace ssl = boost::asio::ssl;

Client::Client(std::shared_ptr<boost::asio::io_context> context)
    : m_context { context }
    , m_sslContext { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , m_log { std::make_shared<Log>("client.txt") }
{
    std::cout << "Client is starting...\n";
}

Client::~Client() {
    std::cout << "Client closed...\n";
}

void Client::Connect(const char *address, int port) {
    m_connection = std::make_shared<Connection>(id++
        , this->weak_from_this()
        , m_log
        , m_context
        , m_sslContext);
    m_connection->Start(address, port);
    std::cout << "Initiate connection ...\n";
}

void Client::Reset() {
    m_connection.reset();
    std::cout << "Reset connection ...\n";
}

void Client::Run() {
    const char *ADDRESS = "127.0.0.1";
    const int PORT = 15001;
    this->Connect(ADDRESS, PORT);

    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < 4; i++) {
        threads.emplace_back([this]() {
            for (;;) {
                try {
                    m_context->run();
                    break;
                }
                catch (std::exception const& ex) {
                    m_log->Write(LogType::error, ex.what(), '\n');
                }
            }
        });
    }

    for (auto&&t: threads) {
        t.join();
    }
}