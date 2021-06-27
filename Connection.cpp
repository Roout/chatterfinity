#include "Connection.hpp"
#include "Utility.hpp"

#include <cassert>
#include <functional>
#include <algorithm>

#include <boost/format.hpp>

Connection::Connection(io_context_pointer context
    , ssl_context_pointer sslContext
    , size_t id
    , std::string_view host
)
    : m_context { context }
    , m_sslContext { sslContext }
    , m_resolver { *context }
    , m_strand { *context }
    , m_socket { *context, *sslContext }
    , m_id { id }
    , m_host { host }
    , m_log { std::make_shared<Log>( (boost::format("connection_%1%.txt") % id).str().data() ) }
{
    // Perform SSL handshake and verify the remote host's certificate.
    m_socket.set_verify_mode(ssl::verify_peer);
    m_socket.set_verify_callback(ssl::rfc2818_verification(host.data()));
}

Connection::~Connection() { 
    // No need to call `close` through executor via `boost::asio::post(...)`
    // because last instance of `shared_ptr` is already destroyed
    Close();
    m_log->Write(LogType::info, "Connection destroyed\n"); 
}

void Connection::InitiateSocketShutdown() {
    boost::asio::post(m_strand, [weakSelf = this->weak_from_this()](){
        if (auto origin = weakSelf.lock(); origin) {
            origin->Close();
        }
    });
}

net::http::Message Connection::AcquireResponse() noexcept {
    return { std::move(m_header), std::move(m_body) };
}

void Connection::Write(std::string text, std::function<void()> onSuccess) {
    m_outbox = std::move(text);
    m_onSuccess = std::move(onSuccess);
    m_resolver.async_resolve(m_host
        , kService
        , boost::asio::bind_executor(m_strand
            , std::bind(&Connection::OnResolve
                , shared_from_this()
                , std::placeholders::_1
                , std::placeholders::_2
            )
        )
    );
}

void Connection::OnResolve(const boost::system::error_code& error
    , tcp::resolver::results_type results
) {
    if (error) {
        m_log->Write(LogType::error, m_id, "failed to resolve the host\n");
    }
    else {
        boost::asio::async_connect(m_socket.lowest_layer()
            , results
            , boost::asio::bind_executor(m_strand
                , std::bind(&Connection::OnConnect
                    , shared_from_this()
                    , std::placeholders::_1
                    , std::placeholders::_2
                )
            )
        );
    }
}

void Connection::OnConnect(const boost::system::error_code& error
    , const boost::asio::ip::tcp::endpoint& endpoint
) {
    if (error) {
        m_log->Write(LogType::error, m_id , "failed to connect:", error.message(), "\n");
        InitiateSocketShutdown();
    } 
    else {
        m_log->Write(LogType::info, m_id, "connected. Local port:", endpoint, '\n');
        m_socket.async_handshake(boost::asio::ssl::stream_base::client
            , boost::asio::bind_executor(m_strand
                , std::bind(&Connection::OnHandshake
                    , shared_from_this()
                    , std::placeholders::_1
                )
            )
        );
    }
}

void Connection::OnHandshake(const boost::system::error_code& error) {
    if (error) {
        m_log->Write(LogType::error, "Handshake failed:", error.message(), "\n");
        InitiateSocketShutdown();
    }
    else {
        assert(!m_outbox.empty());
        WriteBuffer();
    }
}

void Connection::WriteBuffer() {
    boost::asio::async_write(
        m_socket,
        boost::asio::const_buffer(m_outbox.data(), m_outbox.size()),
        boost::asio::bind_executor(m_strand,
            std::bind(&Connection::OnWrite, 
                shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnWrite(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        m_log->Write(LogType::error, m_id, "fail OnWrite:", error.message(), '\n');
        InitiateSocketShutdown();
    }
    else {
        m_log->Write(LogType::info, m_id, "sent:", bytes, "bytes\n");
        // initiate read chain!
        ReadHeader();
    } 
}

void Connection::ReadHeader() {
    m_body.clear();
    m_chunk.Reset();
    m_inbox.consume(m_inbox.size());
    
    boost::asio::async_read_until(m_socket
        , m_inbox
        , kHeaderDelimiter
        , boost::asio::bind_executor(m_strand
            , std::bind(&Connection::OnHeaderRead, 
                shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnHeaderRead(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        m_log->Write(LogType::error, m_id, "failed OnHeaderRead", error.message(), "\n");
        InitiateSocketShutdown();
    } 
    else {
        if (error == boost::asio::error::eof) {
           m_log->Write(LogType::warning, m_id, "failed OnHeaderRead meet EOF", error.message(), "\n");
        }
        m_log->Write(LogType::info, m_id, "OnHeaderRead read", bytes, "bytes.\n");

        const auto data { m_inbox.data() };
        const std::string header {
            boost::asio::buffers_begin(data), 
            boost::asio::buffers_begin(data) + bytes - kHeaderDelimiter.size()
        };        
        m_inbox.consume(bytes);
        m_header = net::http::ParseHeader(header);
        // TODO: Handle status code!
        // print status line
        m_log->Write(LogType::info, m_id
            , m_header.m_httpVersion
            , m_header.m_statusCode
            , m_header.m_reasonPhrase, '\n'
        );
        assert(m_header.m_bodyKind != net::http::BodyContentKind::unknown);
        
        using net::http::BodyContentKind;
        switch (m_header.m_bodyKind) {
            case BodyContentKind::chunkedTransferEncoded: {
                m_body.clear();
                m_chunk.Reset();
                ReadChunkedBody(); 
            } break;
            case BodyContentKind::contentLengthSpecified: {
                if (m_inbox.size()) {
                    m_body.assign(
                        boost::asio::buffers_begin(data), 
                        boost::asio::buffers_begin(data) + m_inbox.size()
                    );
                    m_inbox.consume(m_inbox.size());
                }
                ReadIntactBody(); 
            } break;
            case BodyContentKind::unknown: [[fallthrough]];
            default: 
                throw std::runtime_error("Problem with header parsing occured");
        }
    }
}

void Connection::ReadChunkedBody() {
    assert(m_header.m_bodyKind == net::http::BodyContentKind::chunkedTransferEncoded);

    boost::asio::async_read_until(m_socket
        , m_inbox
        , kCRLF
        , boost::asio::bind_executor(m_strand
            , std::bind(&Connection::OnReadChunkedBody, 
                shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnReadChunkedBody(const boost::system::error_code& error, size_t bytes) {
    if (error && error != boost::asio::error::eof) {
        m_log->Write(LogType::error, m_id, "failed OnReadChunkedBody", error.message(), "\n");
        InitiateSocketShutdown();
        return;
    } 
    if (error == boost::asio::error::eof) {
        m_log->Write(LogType::warning, m_id, "failed OnReadChunkedBody meet EOF", error.message(), "\n");
    }

    m_log->Write(LogType::info, m_id, "OnReadChunkedBody read", bytes, "bytes.\n");
    const auto data = m_inbox.data();
    std::string chunk {
        boost::asio::buffers_begin(data), 
        boost::asio::buffers_begin(data) + bytes - kCRLF.size()
    };
    m_inbox.consume(bytes);
    if (m_chunk.m_consumed & 1) { 
        // chunk content
        m_log->Write(LogType::info, m_id, "chunk:", m_chunk.m_consumed / 2, ":", chunk, '\n');
        m_chunk.m_consumed++;
        if (!m_chunk.m_size) {
            // This is the last part of 0 chunk so notify about that subscribers
            // Body has been already read
            m_log->Write(LogType::info, m_id, "read body:\n", m_body, '\n');
            if (m_onSuccess) {
                std::invoke(m_onSuccess);
            }
            return;
        };
        m_body.append(chunk);
    }
    else { 
        // chunk size
        m_chunk.m_size = utils::ExtractInteger(chunk, 16);
        m_chunk.m_consumed++;
        m_log->Write(LogType::info, m_id, "chunk:", m_chunk.m_consumed / 2, "of size", m_chunk.m_size, '\n');
    }
    // read next line
    ReadChunkedBody();
}

void Connection::ReadIntactBody() {
    assert(m_header.m_bodyKind == net::http::BodyContentKind::contentLengthSpecified);
    static constexpr size_t kChunkSize = 1024;
    // size of the content I need to parse from the HTTP response
    const auto bodyExpectedSize = static_cast<size_t>(m_header.m_bodyLength);
    const auto parsedContentSize = m_body.size();
    if (parsedContentSize < bodyExpectedSize) {
        const auto minChunk = std::min(kChunkSize, bodyExpectedSize - parsedContentSize);
        boost::asio::async_read(m_socket
            , m_inbox
            , boost::asio::transfer_exactly(minChunk)
            , boost::asio::bind_executor(m_strand
                , std::bind(&Connection::OnReadIntactBody, 
                    shared_from_this(), 
                    std::placeholders::_1, 
                    std::placeholders::_2
                )
            )
        );
    }
    else {
        // NOTIFY that we have read body sucessfully
        m_log->Write(LogType::info, m_id, "read intact body:", m_body, '\n');
        if (m_onSuccess) {
            std::invoke(m_onSuccess);
        }
    }
}

void Connection::OnReadIntactBody(const boost::system::error_code& error, size_t bytes) {
    if (error && error != boost::asio::error::eof) {
        m_log->Write(LogType::error, m_id, "failed OnReadIntactBody", error.message(), "\n");
        // required for the case when a connection still has outgoing write or other operation
        // so shared_ptr's strong_refs > 0
        InitiateSocketShutdown();
        return;
    } 
    if (error == boost::asio::error::eof) {
        m_log->Write(LogType::warning, m_id, "failed OnReadIntactBody meet EOF", error.message(), "\n");
    }
    m_log->Write(LogType::info, m_id, "OnReadIntactBody read", bytes, "bytes.\n");
    ReadIntactBody();
}

void Connection::Close() {
    boost::system::error_code error;
    m_socket.shutdown(error);
    if (error) {
        m_log->Write(LogType::error, "SSL socket shutdown:", error.message(), '\n');
        error.clear();
    }
    m_socket.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    if (error) {
        m_log->Write(LogType::error, "SSL underlying socket shutdown:", error.message(), '\n');
        error.clear();
    }
    m_socket.lowest_layer().close(error);
    if (error) {
        m_log->Write(LogType::error, "SSL underlying socket close:", error.message(), '\n');
    }
}
