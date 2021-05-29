#include "Connection.hpp"
#include "Client.hpp"
#include "Logger.hpp"

#include <functional>
#include <algorithm>

#include <boost/format.hpp>

using boost::asio::ip::tcp;

Connection::Connection(std::size_t id
    , std::weak_ptr<Client> client
    , std::shared_ptr<Log> log
    , std::shared_ptr<boost::asio::io_context> context
    , std::shared_ptr<boost::asio::ssl::context> sslContext
)
    : m_id { id }
    , m_client { client }
    , m_log { log }
    , m_context { context }
    , m_sslContext { sslContext }
    , m_socket { *context, *m_sslContext }
    , m_strand { *context }
{
}

void Connection::Start(const char *address, const char * protocol) {
    boost::asio::ip::tcp::resolver resolver(*m_context);
    const auto endpoints = resolver.resolve(address, protocol);
    if (endpoints.empty()) {
        this->Shutdown();
    }
    else {
        boost::asio::async_connect(
            m_socket.lowest_layer(),
            endpoints, 
            std::bind(&Connection::OnConnect, this->shared_from_this(), std::placeholders::_1, std::placeholders::_2)
        );
    }
}

void Connection::Write(std::string text) {
    m_outbox.Enque(std::move(text));
    if (m_isIdle) {
        this->WriteBuffer();
    } 
}

void Connection::Read() {
    boost::asio::async_read_until(
        m_socket
        , m_inbox
        , DELIMITER.data()
        , std::bind(&Connection::OnRead, 
            this->shared_from_this(), 
            std::placeholders::_1, 
            std::placeholders::_2
        )
    );
}

void Connection::OnRead(
    const boost::system::error_code& error
    , std::size_t bytes
) {
    if (!error) {
        m_log->Write(LogType::info, 
            "Client just recive:", bytes, "bytes.\n"
        );
        
        const auto data { m_inbox.data() }; // asio::streambuf::const_buffers_type
        std::string received {
            boost::asio::buffers_begin(data), 
            boost::asio::buffers_begin(data) + bytes - DELIMITER.size()
        };
        
        m_inbox.consume(bytes);
        
        boost::system::error_code error; 
        m_log->Write(LogType::info, 
            m_socket.lowest_layer().remote_endpoint(error), ":", received, '\n' 
        );
        this->Read();
    } 
    else {
        m_log->Write(LogType::error, 
            "Client", m_id, "failed to read with error: ", error.message(), "\n"
        );
        this->Shutdown();
    }
}

void Connection::WriteBuffer() {
    m_isIdle = false;
    m_outbox.SwapBuffers();
    boost::asio::async_write(
        m_socket,
        m_outbox.GetBufferSequence(),
        boost::asio::bind_executor(
            m_strand,
            std::bind(&Connection::OnWrite, 
                this->shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnConnect(
    const boost::system::error_code& error
    , const boost::asio::ip::tcp::endpoint& endpoint
) {
    if (error) {
        m_log->Write(LogType::error, 
            "Client", m_id , "failed to connect with error: ", error.message(), "\n" 
        );
        this->Shutdown();
    } 
    else {
        m_log->Write(LogType::info
            , "Client"
            , m_id
            , "connected successfully. Used local port:"
            , endpoint
            , '\n');

        this->Handshake();
    }
}

void Connection::OnWrite(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        m_isIdle = true;
        m_log->Write(LogType::error, 
            "Client has error on writting: ", error.message(), '\n'
        );
        this->Shutdown();
    }
    else {
        m_log->Write(LogType::info, 
            "Client", m_id, "just sent: ", bytes, " bytes\n"
        );
        if (m_outbox.GetQueueSize()) {
            this->WriteBuffer();
        } 
        else {
            m_isIdle = true;
        }
    } 
    
}

void Connection::Shutdown() {
    boost::asio::post(m_strand, [self = this->shared_from_this()]() {
        boost::system::error_code error;
        self->m_socket.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
        if (error) {
            self->m_log->Write(LogType::error, 
                "Connection's socket called shutdown with error: ", error.message(), '\n'
            );
            error.clear();
        }
        
        self->m_socket.lowest_layer().close(error);
        if (error) {
            self->m_log->Write(LogType::error, 
                "Connection's socket is being closed with error: ", error.message(), '\n'
            );
        }
        self->m_isIdle = true;

        if(auto model = self->m_client.lock(); model) {
            model->Reset();
        }
    });
}

void Connection::Handshake() {
    m_socket.async_handshake(boost::asio::ssl::stream_base::client,
        [self = this->shared_from_this()] (const boost::system::error_code& error) {
            if (!error) {
                // auto text = boost::format("endpoint=%1% with id=%2% joined.%3%") 
                //     % m_socket.lowest_layer().local_endpoint() 
                //     % m_id 
                //     % DELIMITER;
                // this->Write(text.str());
                // this->Read();

                // start waiting incoming calls
                self->Read();
            }
            else {
                self->m_log->Write(LogType::error, "Handshake failed:", error.message(), "\n");
            }
        }
    );
}