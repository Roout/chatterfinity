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
    , std::string_view service
)
    : context_ { context }
    , sslContext_ { sslContext }
    , resolver_ { *context }
    , strand_ { *context }
    , socket_ { *context, *sslContext }
    , id_ { id }
    , host_ { host }
    , service_ { service }
    , log_ { std::make_shared<Log>( (boost::format("%1%_connection_%2%.txt") % host % id).str().data() ) }
{
    // Perform SSL handshake and verify the remote host's certificate.
    socket_.set_verify_mode(ssl::verify_peer);
    socket_.set_verify_callback(ssl::rfc2818_verification(host.data()));
}

Connection::~Connection() { 
    // No need to call `close` through executor via `boost::asio::post(...)`
    // because last instance of `shared_ptr` is already destroyed
    Close();
    log_->Write(LogType::info, "destroyed\n"); 
}

void Connection::InitiateSocketShutdown() {
    boost::asio::post(strand_, [weakSelf = weak_from_this()](){
        if (auto origin = weakSelf.lock(); origin) {
            origin->Close();
        }
    });
}

void Connection::Connect(std::function<void()> onConnect) {
    onConnectSuccess_ = std::move(onConnect);
    resolver_.async_resolve(host_
        , service_
        , boost::asio::bind_executor(strand_
            , std::bind(&Connection::OnResolve
                , shared_from_this()
                , std::placeholders::_1
                , std::placeholders::_2
            )
        )
    );
}

void Connection::Write(std::string text, std::function<void()> onWrite) {
    outbox_ = std::move(text);
    onWriteSuccess_ = std::move(onWrite);

    boost::asio::async_write(
        socket_,
        boost::asio::const_buffer(outbox_.data(), outbox_.size()),
        boost::asio::bind_executor(strand_,
            std::bind(&Connection::OnWrite, 
                shared_from_this(), 
                std::placeholders::_1, 
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnResolve(const boost::system::error_code& error
    , tcp::resolver::results_type results
) {
    if (error) {
        log_->Write(LogType::error, "failed to resolve the host\n");
    }
    else {
        boost::asio::async_connect(socket_.lowest_layer()
            , results
            , boost::asio::bind_executor(strand_
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
        log_->Write(LogType::error, id_ , "failed to connect:", error.message(), "\n");
        InitiateSocketShutdown();
    } 
    else {
        log_->Write(LogType::info, "connected. Local port:", endpoint, '\n');
        socket_.async_handshake(boost::asio::ssl::stream_base::client
            , boost::asio::bind_executor(strand_
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
        log_->Write(LogType::error, "handshake failed:", error.message(), "\n");
        InitiateSocketShutdown();
    }
    else {
        log_->Write(LogType::info, "handshake successeded.\n");
        if (onConnectSuccess_) {
            std::invoke(onConnectSuccess_);
        }
    }
}

void Connection::OnWrite(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        log_->Write(LogType::error, "fail OnWrite:", error.message(), '\n');
        InitiateSocketShutdown();
    }
    else {
        log_->Write(LogType::info, "sent:", bytes, "bytes\n");
        if (onWriteSuccess_) {
            std::invoke(onWriteSuccess_);
        }
    } 
}

void Connection::Close() {
    boost::system::error_code error;
    socket_.shutdown(error);
    if (error) {
        log_->Write(LogType::error, "SSL socket shutdown:", error.message(), '\n');
        error.clear();
    }
    socket_.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
    if (error) {
        log_->Write(LogType::error, "SSL underlying socket shutdown:", error.message(), '\n');
        error.clear();
    }
    socket_.lowest_layer().close(error);
    if (error) {
        log_->Write(LogType::error, "SSL underlying socket close:", error.message(), '\n');
    }
}

void HttpConnection::Read(std::function<void()> onSuccess) {
    onReadSuccess_ = std::move(onSuccess);
    ReadHeader();
}

void HttpConnection::ReadHeader() {
    body_.clear();
    chunk_.Reset();
    inbox_.consume(inbox_.size());
    
    boost::asio::async_read_until(socket_
        , inbox_
        , kHeaderDelimiter
        , boost::asio::bind_executor(strand_
            , std::bind(&HttpConnection::OnHeaderRead
                , utils::SharedFrom<HttpConnection>(shared_from_this())
                , std::placeholders::_1
                , std::placeholders::_2
            )
        )
    );
}

void HttpConnection::OnHeaderRead(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        log_->Write(LogType::error, "failed OnHeaderRead", error.message(), "\n");
        InitiateSocketShutdown();
    } 
    else {
        if (error == boost::asio::error::eof) {
            log_->Write(LogType::warning, "failed OnHeaderRead meet EOF", error.message(), "\n");
        }
        log_->Write(LogType::info, "OnHeaderRead read", bytes, "bytes.\n");

        {
            const auto data { inbox_.data() };
            const std::string header {
                boost::asio::buffers_begin(data), 
                boost::asio::buffers_begin(data) + bytes - kHeaderDelimiter.size()
            };        
            inbox_.consume(bytes);
            header_ = net::http::ParseHeader(header);
        }
        // TODO: Handle status code!
        // print status line
        log_->Write(LogType::info
            , header_.httpVersion_
            , header_.statusCode_
            , header_.reasonPhrase_, '\n'
        );
        assert(header_.bodyKind_ != net::http::BodyContentKind::unknown);
        
        using net::http::BodyContentKind;
        switch (header_.bodyKind_) {
            case BodyContentKind::chunkedTransferEncoded: {
                body_.clear();
                chunk_.Reset();
                ReadChunkedBody(); 
            } break;
            case BodyContentKind::contentLengthSpecified: {
                if (inbox_.size()) {
                    body_.assign(
                        boost::asio::buffers_begin(inbox_.data()), 
                        boost::asio::buffers_begin(inbox_.data()) + inbox_.size()
                    );
                    inbox_.consume(inbox_.size());
                }
                ReadIntactBody(); 
            } break;
            case BodyContentKind::unknown: [[fallthrough]];
            default: 
                throw std::runtime_error("Problem with header parsing occured");
        }
    }
}

void HttpConnection::ReadIntactBody() {
    assert(header_.bodyKind_ == net::http::BodyContentKind::contentLengthSpecified);
    static constexpr size_t kChunkSize = 1024;
    // size of the content I need to parse from the HTTP response
    const auto bodyExpectedSize = static_cast<size_t>(header_.bodyLength_);
    const auto parsedContentSize = body_.size();
    if (parsedContentSize < bodyExpectedSize) {
        const auto minChunk = std::min(kChunkSize, bodyExpectedSize - parsedContentSize);
        boost::asio::async_read(socket_
            , inbox_
            , boost::asio::transfer_exactly(minChunk)
            , boost::asio::bind_executor(strand_
                , std::bind(&HttpConnection::OnReadIntactBody
                    // 1. reader raw ptr is valid as long as connection creator (Service) is alive
                    // liftime(reader) > lifetime(connection)
                    , utils::SharedFrom<HttpConnection>(shared_from_this())
                    , std::placeholders::_1 
                    , std::placeholders::_2
                )
            )
        );
    }
    else {
        // NOTIFY that we have read body sucessfully
        log_->Write(LogType::info, "read intact body:", body_, '\n');
        if (onReadSuccess_) {
            std::invoke(onReadSuccess_);
        }
    }
}

void HttpConnection::OnReadIntactBody(const boost::system::error_code& error, size_t bytes) {
    if (error && error != boost::asio::error::eof) {
        log_->Write(LogType::error, "failed OnReadIntactBody", error.message(), "\n");
        // required for the case when a connection still has outgoing write or other operation
        // so shared_ptr's strong_refs > 0
        InitiateSocketShutdown();
        return;
    } 
    if (error == boost::asio::error::eof) {
        log_->Write(LogType::warning, "failed OnReadIntactBody meet EOF", error.message(), "\n");
    }
    log_->Write(LogType::info, "OnReadIntactBody read", bytes, "bytes.\n");
 
    auto data = inbox_.data();
    std::string chunk {
        boost::asio::buffers_begin(data), 
        boost::asio::buffers_begin(data) + bytes
    };
    inbox_.consume(bytes);
    body_.append(chunk);
    // continue to read
    ReadIntactBody();
}

void HttpConnection::ReadChunkedBody() {
    assert(header_.bodyKind_ == net::http::BodyContentKind::chunkedTransferEncoded);

    boost::asio::async_read_until(socket_
        , inbox_
        , kCRLF
        , boost::asio::bind_executor(strand_
            , std::bind(&HttpConnection::OnReadChunkedBody
                , utils::SharedFrom<HttpConnection>(shared_from_this())
                , std::placeholders::_1 
                , std::placeholders::_2
            )
        )
    );
}

void HttpConnection::OnReadChunkedBody(const boost::system::error_code& error, size_t bytes) {
    if (error && error != boost::asio::error::eof) {
        log_->Write(LogType::error, "failed OnReadChunkedBody", error.message(), "\n");
        InitiateSocketShutdown();
        return;
    } 
    if (error == boost::asio::error::eof) {
        log_->Write(LogType::warning, "failed OnReadChunkedBody meet EOF", error.message(), "\n");
    }

    log_->Write(LogType::info, "OnReadChunkedBody read", bytes, "bytes.\n");
    const auto data = inbox_.data();
    std::string chunk {
        boost::asio::buffers_begin(data), 
        boost::asio::buffers_begin(data) + bytes - kCRLF.size()
    };
    inbox_.consume(bytes);
    if (chunk_.consumed_ & 1) { 
        // chunk content
        log_->Write(LogType::info, "chunk:", chunk_.consumed_ / 2, ":", chunk, '\n');
        chunk_.consumed_++;
        if (!chunk_.size_) {
            // This is the last part of 0 chunk so notify about that subscribers
            // Body has been already read
            log_->Write(LogType::info, "read body:\n", body_, '\n');
            if (onReadSuccess_) {
                std::invoke(onReadSuccess_);
            }
            return;
        };
        body_.append(chunk);
    }
    else { 
        // chunk size
        chunk_.size_ = utils::ExtractInteger(chunk, 16);
        chunk_.consumed_++;
        log_->Write(LogType::info, "chunk:", chunk_.consumed_ / 2, "of size", chunk_.size_, '\n');
    }
    // read next line
    ReadChunkedBody();
}

void IrcConnection::Read(std::function<void()> onSuccess) {
    onReadSuccess_ = std::move(onSuccess);
    inbox_.consume(inbox_.size());

    boost::asio::async_read_until(socket_
        , inbox_
        , kCRLF
        , boost::asio::bind_executor(strand_
            , std::bind(&IrcConnection::OnRead
                , utils::SharedFrom<IrcConnection>(shared_from_this())
                , std::placeholders::_1
                , std::placeholders::_2
            )
        )
    );
}

void IrcConnection::OnRead(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        log_->Write(LogType::error, "failed OnRead", error.message(), "\n");
        InitiateSocketShutdown();
    } 
    else {
        if (error == boost::asio::error::eof) {
            log_->Write(LogType::warning, "failed IrcConnection::OnRead meet EOF", error.message(), "\n");
        }
        log_->Write(LogType::info, "IrcConnection::OnRead read", bytes, "bytes.\n");

        const auto data { inbox_.data() };
        std::string buffer {
            boost::asio::buffers_begin(data), 
            boost::asio::buffers_begin(data) + bytes - kCRLF.size()
        };        
        inbox_.consume(bytes);
        message_ = net::irc::ParseMessage(std::string_view { buffer.data(), buffer.size() } );

        if (onReadSuccess_) {
            std::invoke(onReadSuccess_);
        }
    }
}