#include "Connection.hpp"

#include <cassert>
#include <functional>
#include <algorithm>

#include <boost/format.hpp>

using boost::asio::ip::tcp;

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

void Connection::Write(std::string text) {
    m_outbox = std::move(text);
    m_resolver.async_resolve(m_host
        , kService
        , boost::asio::bind_executor(m_strand
            , std::bind(&Connection::OnResolve
                , this->shared_from_this()
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
                    , this->shared_from_this()
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
        this->Close();
    } 
    else {
        m_log->Write(LogType::info, m_id, "connected. Local port:", endpoint, '\n');
        m_socket.async_handshake(boost::asio::ssl::stream_base::client
            , boost::asio::bind_executor(m_strand
                , std::bind(&Connection::OnHandshake
                    , this->shared_from_this()
                    , std::placeholders::_1
                )
            )
        );
    }
}

void Connection::OnHandshake(const boost::system::error_code& error) {
    if (error) {
        m_log->Write(LogType::error, "Handshake failed:", error.message(), "\n");
        this->Close();
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
                this->shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnWrite(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        m_log->Write(LogType::error, m_id, "fail OnWrite:", error.message(), '\n');
        this->Close();
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
                this->shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnHeaderRead(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        m_log->Write(LogType::error, m_id, "failed OnHeaderRead", error.message(), "\n");
        this->Close();
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
        m_header = blizzard::ParseHeader(header);
        // TODO: Handle status code!
        // print status line
        m_log->Write(LogType::info, m_id
            , m_header.m_httpVersion
            , m_header.m_statusCode
            , m_header.m_reasonPhrase, '\n'
        );
        assert(m_header.m_bodyKind != blizzard::BodyContentKind::unknown);
        
        using blizzard::BodyContentKind;
        switch(m_header.m_bodyKind) {
            case BodyContentKind::chunkedTransferEncoded: {
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
    assert(false && "TODO: implement ReadChunkedBody");
}

void Connection::OnReadChunkedBody(const boost::system::error_code& error, size_t bytes) {
    assert(false && "TODO: implement OnReadChunkedBody");
}

void Connection::ReadIntactBody() {
    assert(m_header.m_bodyKind == blizzard::BodyContentKind::chunkedTransferEncoded);
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
                    this->shared_from_this(), 
                    std::placeholders::_1, 
                    std::placeholders::_2
                )
            )
        );
    }
    else {
        // NOTIFY that we have read body sucessfully
        m_log->Write(LogType::info, m_id, "read intact body:", m_body, '\n');
    }
}

void Connection::OnReadIntactBody(const boost::system::error_code& error, size_t bytes) {

}

void Connection::Close() {
    boost::asio::post(m_strand, [self = this->shared_from_this()]() {
        boost::system::error_code error;
        self->m_socket.shutdown(error);
        if (error) {
            self->m_log->Write(LogType::error, "SSL socket shutdown:", error.message(), '\n');
            error.clear();
        }
        self->m_socket.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
        if (error) {
            self->m_log->Write(LogType::error, "SSL underlying socket shutdown:", error.message(), '\n');
            error.clear();
        }
        self->m_socket.lowest_layer().close(error);
        if (error) {
            self->m_log->Write(LogType::error, "SSL underlying socket close:", error.message(), '\n');
        }
    });
}
