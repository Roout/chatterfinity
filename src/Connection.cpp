#include "Connection.hpp"
#include "Utility.hpp"

#include <cassert>
#include <functional>
#include <algorithm>

#include <boost/format.hpp>

Connection::Connection(SharedIOContext context
    , SharedSSLContext sslContext
    , std::string_view host
    , std::string_view service
    , size_t id
)
    : context_ { context }
    , ssl_ { sslContext }
    , resolver_ { *context }
    , strand_ { *context }
    , socket_ { std::in_place, *context, *sslContext }
    , timer_ { *context }
    , host_ { host }
    , service_ { service }
    , log_ { std::make_shared<Log>((boost::format("%1%_%2%_%3%.txt") % host % service % id).str().data()) }
    , isWriting_ { false }
{
}

Connection::~Connection() { 
    Close();
    log_->Write(LogType::kInfo, "destroyed\n"); 
}

void Connection::Close() {
    boost::system::error_code error;
    timer_.cancel(error);
    if (error) {
        log_->Write(LogType::kError, "timer cancel:", error.message(), '\n');
        error.clear();
    }

    assert(socket_.has_value() && "optional can't be empty"
        "(maybe there is data race where emplace is being called)"
    );

    socket_->shutdown(error);
    if (error) {
        log_->Write(LogType::kError
            , "SSL stream shutdown:"
            , error.message(), '\n');
        error.clear();
    }
    if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
        ll.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
        if (error) {
            log_->Write(LogType::kError
                , "SSL underlying socket shutdown:"
                , error.message(), '\n');
            error.clear();
        }
        ll.close(error);
        if (error) {
            log_->Write(LogType::kError
                , "SSL underlying socket close:"
                , error.message(), '\n');
        }
    } 
}

void Connection::ScheduleShutdown() {
    boost::asio::post(strand_, [weakSelf = weak_from_this()](){
        if (auto origin = weakSelf.lock(); origin) {
            origin->Close();
        }
    });
}

void Connection::Connect(std::function<void()> onConnect) { 
    if (onConnect) {
        onConnectSuccess_ = std::move(onConnect);
    }
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

void Connection::OnResolve(const boost::system::error_code& error
    , tcp::resolver::results_type results
) {
    if (error) {
        log_->Write(LogType::kError, "OnResolve:", error.message(), '\n');
        if (error != boost::asio::error::operation_aborted) {
            Reconnect();
        }
    }
    else {
        // configure socket
        // 1. setup verification process settings
        socket_->set_verify_mode(ssl::verify_peer);
        socket_->set_verify_callback(ssl::rfc2818_verification(host_.data()));
 
        // 2. set SNI Hostname (many hosts need this to handshake successfully)
        // TLS-SNI (without this option, handshakes attempts with hosts behind CDNs will fail,
        // due to the fact that the CDN does not have enough information at the TLS layer
        // to decide where to forward the handshake attempt).
        // Source: https://github.com/chriskohlhoff/asio/issues/262
        if (!SSL_set_tlsext_host_name(socket_->native_handle(), host_.data())){
            boost::system::error_code ec{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() };
            throw boost::system::system_error{ ec };
        }

        boost::asio::async_connect(socket_->lowest_layer()
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
        log_->Write(LogType::kError, "OnConnect:", error.message(), "\n");
        if (error != boost::asio::error::operation_aborted) {
            Reconnect();
        }
    } 
    else {
        log_->Write(LogType::kInfo, "connected. Local port:", endpoint, '\n');
        socket_->async_handshake(boost::asio::ssl::stream_base::client
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
        log_->Write(LogType::kError, "OnHandshake:", error.message(), "\n");
        if (error != boost::asio::error::operation_aborted) {
            Reconnect();
        }
    }
    else {
        log_->Write(LogType::kInfo, "handshake successeded.\n");
        reconnects_ = 0;
        if (onConnectSuccess_) {
            std::invoke(onConnectSuccess_);
        }
    }
}

void Connection::Reconnect() {
    Close();
    // prepare for reconnection
    socket_.emplace(*context_, *ssl_);
    // start timer
    const auto timeout = 1u << ++reconnects_; // in seconds
    timer_.expires_from_now(boost::posix_time::seconds{ timeout });
    timer_.async_wait(
        boost::asio::bind_executor(strand_, // MUST user strand executor to prevent data-race
            std::bind(&Connection::OnTimeout, shared_from_this(), std::placeholders::_1)
        )
    );
    log_->Write(LogType::kInfo, "reconnecting after", timeout, "seconds ...\n");
}

void Connection::ScheduleWrite(std::string text, std::function<void()> onWrite) {
    auto deferredCallee = [text = std::move(text)
        , callback = std::move(onWrite)
        , self = shared_from_this()
    ]() mutable {
        self->outbox_.Enque(std::move(text), std::move(callback));
        if (!self->isWriting_) {
            self->Write();
        }
    };
    // [CRITICAL SECTION]
    // ASSUMED it's thread-safe to `post` completion token through `strand_`
    // see https://stackoverflow.com/questions/51859895/is-boostasioio-servicepost-atomic/51862417
    boost::asio::post(strand_, std::move(deferredCallee));
}

void Connection::Write() {
    // add all text that is queued for write operation to active buffer
    outbox_.SwapBuffers();
    isWriting_ = true;
    boost::asio::async_write(*socket_,
        outbox_.GetBufferSequence(),
        boost::asio::bind_executor(strand_,
            std::bind(&Connection::OnWrite,
                shared_from_this(),
                std::placeholders::_1,
                std::placeholders::_2
            )
        )
    );
}

void Connection::OnWrite(const boost::system::error_code& error, size_t bytes) {
    isWriting_ = false;

    if (error) {
        log_->Write(LogType::kError, "OnWrite:", error.message(), '\n');
        assert(socket_);
        if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
            // confirm that socket is open to prevent reconnection after shutdown
            Reconnect();
        }
    }
    else {
        const auto& sequence = outbox_.GetBufferSequence();
        // dump data we're sending
        // TODO: escape special characters
        for (size_t i = 0; i < sequence.size(); i++) {
            const auto buf = sequence[i];
            std::string_view dump { static_cast<const char*>(buf.data()), buf.size() };
            log_->Write(LogType::kInfo, i, "sent", bytes, "bytes :", utils::Trim(dump), "\n");
        }
        // as we successfully send all data to remote peer
        // we can now invoke all callbacks which corresponds to these data
        for (const auto& cb: outbox_.GetCallbackSequence()) {
            std::invoke(cb);
        }
        if (outbox_.GetQueueSize()) {
            // there are a few messages scheduled to be sent
            log_->Write(LogType::kInfo, "queued messages:", outbox_.GetQueueSize(), "\n");
            Write();
        }
    } 
}

void Connection::OnTimeout(const boost::system::error_code& error) {
    if (error == boost::asio::error::operation_aborted) {
        // is being closed while waiting
        log_->Write(LogType::kInfo, "OnTimeout:", error.message(), '\n');
    }
    else if (error) {
        log_->Write(LogType::kError, "OnTimeout:", error.message(), '\n');
        Close();
    }
    else if (reconnects_ > kReconnectLimit) {
        log_->Write(LogType::kWarning, "OnTimeout: reach reconnection limit\n");
        Close();
    }
    else {
        log_->Write(LogType::kInfo, "OnTimeout: start connection\n");
        // don't provide callback so that it won't overwrite existing OnConnectSuccess_ callback
        Connect();
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
    
    boost::asio::async_read_until(*socket_
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
        log_->Write(LogType::kError, "OnHeaderRead", error.message(), "\n");
        assert(socket_);
        if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
            // prevent reconnection after shutdown
            Reconnect();
        }
    } 
    else {
        { // extract header
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
        log_->Write(LogType::kInfo
            , header_.httpVersion_
            , header_.statusCode_
            , header_.reasonPhrase_, '\n'
        );
        assert(header_.bodyKind_ != net::http::BodyContentKind::kUnknown);
        
        using net::http::BodyContentKind;
        switch (header_.bodyKind_) {
            case BodyContentKind::kChunkedTransferEncoded: {
                body_.clear();
                chunk_.Reset();
                ReadChunkedBody(); 
            } break;
            case BodyContentKind::kContentLengthSpecified: {
                if (inbox_.size()) {
                    body_.assign(
                        boost::asio::buffers_begin(inbox_.data()), 
                        boost::asio::buffers_begin(inbox_.data()) + inbox_.size()
                    );
                    inbox_.consume(inbox_.size());
                }
                ReadIntactBody(); 
            } break;
            case BodyContentKind::kUnknown: [[fallthrough]];
            default: 
                throw std::runtime_error("Problem with header parsing occured");
        }
    }
}

void HttpConnection::ReadIntactBody() {
    assert(header_.bodyKind_ == net::http::BodyContentKind::kContentLengthSpecified);
    static constexpr size_t kChunkSize = 1024;
    // size of the content I need to parse from the HTTP response
    const auto bodyExpectedSize = static_cast<size_t>(header_.bodyLength_);
    const auto parsedContentSize = body_.size();
    if (parsedContentSize < bodyExpectedSize) {
        const auto minChunk = std::min(kChunkSize, bodyExpectedSize - parsedContentSize);
        boost::asio::async_read(*socket_
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
        log_->Write(LogType::kInfo, "ReadIntactBody size:", body_.size(), '\n');
        if (onReadSuccess_) {
            std::invoke(onReadSuccess_);
        }
    }
}

void HttpConnection::OnReadIntactBody(const boost::system::error_code& error, size_t bytes) {
    if (error) {
        log_->Write(LogType::kError, "OnReadIntactBody:", error.message(), "\n");
        assert(socket_);
        if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
            // prevent reconnection after shutdown
            Reconnect();
        }
    }
    else {
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
}

void HttpConnection::ReadChunkedBody() {
    assert(header_.bodyKind_ == net::http::BodyContentKind::kChunkedTransferEncoded);

    boost::asio::async_read_until(*socket_
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
    if (error) {
        log_->Write(LogType::kError, "OnReadChunkedBody:", error.message(), "\n");
        assert(socket_);
        if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
            // condition prevents reconnection attempts after shutdown
            Reconnect();
        }
        return;
    } 

    const auto data = inbox_.data();
    std::string chunk {
        boost::asio::buffers_begin(data), 
        boost::asio::buffers_begin(data) + bytes - kCRLF.size()
    };
    inbox_.consume(bytes);
    if (chunk_.consumed_ & 1) { 
        // chunk content
        chunk_.consumed_++;
        if (!chunk_.size_) {
            // This is the last part of 0 chunk so notify about that subscribers
            // Body has been already read
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
    }
    // read next line
    ReadChunkedBody();
}

void IrcConnection::Read(std::function<void()> onSuccess) {
    if (onSuccess) {
        onReadSuccess_ = std::move(onSuccess);
    }
    boost::asio::async_read_until(*socket_
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
        log_->Write(LogType::kError, "OnRead:", error.message(), "\n");
        assert(socket_);
        if (auto&& ll = socket_->lowest_layer(); ll.is_open()) {
            // prevent reconnection after shutdown
            Reconnect();
        }
    } 
    else {
        const auto data { inbox_.data() };
        std::string buffer {
            boost::asio::buffers_begin(data), 
            boost::asio::buffers_begin(data) + bytes - kCRLF.size()
        };        
        inbox_.consume(bytes);
        log_->Write(LogType::kInfo, "->", buffer, '\n');
        message_ = net::irc::ParseMessage(std::string_view { buffer.data(), buffer.size() } );

        if (onReadSuccess_) {
            std::invoke(onReadSuccess_);
        }
        Read();
    }
}