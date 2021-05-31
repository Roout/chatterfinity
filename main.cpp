#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <charconv>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

namespace temp {

    std::string AsLowerCase(std::string_view str) {
        std::string copy { str };
        for (auto& c: copy) c = std::tolower(c);
        return copy;
    }

    constexpr std::string_view Trim(
        std::string_view text, 
        std::string_view exclude = " \n\r\t\v\0"
    ) {
        if (const size_t leftShift = text.find_first_not_of(exclude); 
            leftShift != std::string_view::npos
        ) {
            text.remove_prefix(leftShift); 
        }
        else {
            return {};
        }

        if (const size_t rightShift = text.find_last_not_of(exclude); 
            rightShift != std::string_view::npos
        ) {
            text.remove_suffix(text.size() - rightShift - 1); 
        }
        else {
            return {};
        }
        
        return text;
    }

    class Client {
    public:
        Client(
            std::shared_ptr<boost::asio::io_context> context
            , std::shared_ptr<ssl::context> ssl
        ) 
            : m_context { context }
            , m_sslContext { ssl }
        {
        }
        
        ~Client() {
            Close();
        }

        // throws
        void Connect(std::string_view host) {
            m_socket.emplace(*m_context, *m_sslContext);

            tcp::resolver resolver(*m_context);
            const auto endpoints = resolver.resolve(host, "https");
            if (endpoints.empty()) {
                std::cout << "[ERROR]: Endpoints not found!\n";
                std::abort();
            }
    
            (void) boost::asio::connect(m_socket->lowest_layer(), endpoints.begin(), endpoints.end());

            // Perform SSL handshake and verify the remote host's certificate.
            m_socket->set_verify_mode(ssl::verify_peer);
            m_socket->set_verify_callback(ssl::rfc2818_verification(host.data()));
            m_socket->handshake(ssl::stream<tcp::socket>::client);
        }

        void Write(const std::string& request) {
            const auto writeBytes = boost::asio::write(
                *m_socket, boost::asio::const_buffer(request.c_str(), request.size())
            );
            std::cout << "-----> Write request:\n" << request << '\n';
        }

        void ReadHeader() {
            constexpr std::string_view delimiter = "\r\n\r\n";
            constexpr std::string_view CRLF = "\r\n";
            
            m_fields.clear();
            m_inbox.consume(m_inbox.size());

            assert(m_socket.has_value() && "optional of ssl socket is null");
            assert(!m_inbox.size() && m_fields.empty() && "Must be empty!");

            boost::system::error_code error;
            const size_t headerLength = boost::asio::read_until(*m_socket, m_inbox, delimiter, error);
            if (error && error != boost::asio::error::eof) {
                throw boost::system::system_error(error);
            }
            assert(headerLength && "Read 0 bytes!");

            const auto data { m_inbox.data() };
            std::string header {
                boost::asio::buffers_begin(data), 
                boost::asio::buffers_begin(data) + headerLength - delimiter.size()
            };
            m_inbox.consume(headerLength);

            // PARSE HEADER: 
            // extract STATUS
            size_t statusEnd = header.find_first_of(CRLF.data());
            assert(statusEnd != std::string::npos && "Header doesn't have a status");
            statusEnd += CRLF.size();
            // extract FIELDS
            for (size_t start = statusEnd, finish = header.find_first_of(CRLF.data(), statusEnd); 
                finish != std::string::npos && start != finish;
                finish = header.find_first_of(CRLF.data(), start)
            ) {
                std::string_view raw { header.data() + start, finish - start };
                auto split = raw.find_first_of(':');
                std::string_view key { raw.data(), split };
                raw.remove_prefix(split + 1);
                m_fields.emplace(AsLowerCase(key), Trim(raw, " "));
                // update start
                start = finish + CRLF.size();
            }
            std::cout << "----> Header\n";
            for (auto const& [key, value]: m_fields) {
                std::cout << key << ":" << value << '\n';
            }
        }

        // partially done according to [chuncks](https://datatracker.ietf.org/doc/html/rfc7230#section-4.1)
        void ReadBodyChucks() {
            constexpr std::string_view CRLF = "\r\n";
            boost::system::error_code error;
            m_body.clear();

            // NOTE: read_until can and usually do read more than just content before CRLF 
            for (size_t bytes = boost::asio::read_until(*m_socket, m_inbox, CRLF, error), chunkCounter = 0; 
                bytes; 
                bytes = boost::asio::read_until(*m_socket, m_inbox, CRLF, error), chunkCounter++
            ) {
                const auto data = m_inbox.data();
                std::string chunk(
                    boost::asio::buffers_begin(data), 
                    boost::asio::buffers_begin(data) + bytes - CRLF.size()
                );

                if (chunkCounter & 1) { 
                    // chunk content
                    std::cout << "Chunk " << chunkCounter / 2 << ':' << chunk << '\n';
                    m_body.append(chunk);
                }
                else { 
                    // chunk size
                    size_t chunkLength = 0;
                    auto [parsed, ec] = std::from_chars(chunk.data(), chunk.data() + chunk.size(), chunkLength, 16);
                    if (ec != std::errc()) {
                        std::cout << "[ERROR]: Unexpected input.\n"
                            << "\tBefore parsing: " << chunk << '\n'
                            << "\tAfter parsing: " << parsed << '\n';
                        std::abort();
                    }
                    std::cout << "Chunk " << chunkCounter / 2 << " size: " << chunkLength << '\n';
                }
                m_inbox.consume(bytes);
                if (chunk.empty()) break;
            }
            // No trailer fields expected!

            if (error && error != boost::asio::error::eof) {
                throw boost::system::system_error(error);
            }

            std::cout << "\tBody:\n" << m_body << "\n";
        }

        void ReadBody() {
            assert(m_socket.has_value() && "optional of ssl socket is null");
            
            const auto savedData { m_inbox.data() };
            const auto savedSize { m_inbox.size() };
            m_body.assign(
                boost::asio::buffers_begin(savedData), 
                boost::asio::buffers_begin(savedData) + savedSize
            );
            m_inbox.consume(savedSize);

            const auto& contentLength = m_fields.at("content-length");
            // size of the content I need to parse from the HTTP response
            size_t bodyExpectedSize = 0;
            const auto [parsed, ec] = std::from_chars(contentLength.data()
                , contentLength.data() + contentLength.size()
                , bodyExpectedSize
                , 10);
            if (ec != std::errc()) {
                std::cout << "[ERROR]: Unexpected input.\n"
                    << "\tBefore parsing: " << contentLength << '\n'
                    << "\tAfter parsing: " << parsed << '\n';
                std::abort();
            }

            boost::system::error_code error;
            auto parsedContentSize = m_body.size();
            constexpr size_t CHUNK_SIZE = 1024;
            while (parsedContentSize < bodyExpectedSize && !error) {
                const std::size_t smallestChunk = std::min(CHUNK_SIZE, bodyExpectedSize - parsedContentSize);
                size_t readBytes = boost::asio::read(*m_socket, m_inbox, boost::asio::transfer_exactly(smallestChunk), error);
                m_body.append(
                    boost::asio::buffers_begin(m_inbox.data()), 
                    boost::asio::buffers_begin(m_inbox.data()) + readBytes
                );
                m_inbox.consume(readBytes);
                parsedContentSize += readBytes;
            }

            if (error && error != boost::asio::error::eof) {
                throw boost::system::system_error(error);
            }

            assert(parsedContentSize == bodyExpectedSize && "Smth terribly wrong");
            std::cout << "\tBody:\n" << m_body << "\n";
        }

        std::string GetBody() const noexcept {
            return std::move(m_body);
        }

        void Close() noexcept {
            assert(m_socket.has_value() && "optional of ssl socket is null");

            boost::system::error_code error;

            m_socket->shutdown(error);
            if (error) {
                std::cout << "SSL socket called shutdown with error: " << error.message() << '\n';
            }
            error.clear();

            m_socket->lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
            if (error) {
                std::cout << "SSL underlying socket called shutdown with error: " << error.message() << '\n';
            }
            error.clear();
            
            m_socket->lowest_layer().close(error);
            if (error) {
                std::cout << "SSL underlying socket called close with error: " << error.message() << '\n';
            }

            m_socket.emplace(*m_context, *m_sslContext);
        }

        bool IsEncoded() const {
            return m_fields.find("transfer-encoding") != m_fields.cend();
        }

    private:
        std::shared_ptr<boost::asio::io_context> m_context;
        std::shared_ptr<ssl::context> m_sslContext;
        // Switched to optional after reading all this stuff:
        // [Suggest to use optional](https://stackoverflow.com/questions/50693708/boost-asio-ssl-not-able-to-receive-data-for-2nd-time-onwards-1st-time-ok)
        // [Provide link to RFC and problem description](https://stackoverflow.com/questions/15312219/need-to-call-sslstreamshutdown-when-closing-boost-asio-ssl-socket)
        // etc
        std::optional<ssl::stream<tcp::socket>> m_socket;

        boost::asio::streambuf m_inbox;
        // parsed
        std::unordered_map<std::string, std::string> m_fields; 
        std::string m_body;
    };
}

int main() {
    std::string request = 
    "POST /oauth/token HTTP/1.1\r\n"
    "Host: eu.battle.net\r\n"                
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Authorization: Basic YTA3YmI5MGE5OTAxNGRlMjkxNjdiNDRhNzJlN2NhMzY6ZnJERTR1SnVaeWM0bXo1RU9heWxlMmROSm8xQksxM08=\r\n"
    "Content-Length: 29\r\n"
    "\r\n"
    "grant_type=client_credentials";

    boost::system::error_code error;
    auto context = std::make_shared<boost::asio::io_context>();
    auto sslContext = std::make_shared<ssl::context>(ssl::context::method::sslv23_client);
    /**
     * [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
     * Cert Chain:
     * DigiCert High Assurance EV Root CA => DigiCert SHA2 High Assurance Server CA => *.battle.net
     * So root cert is DigiCert High Assurance EV Root CA;
     * Valid until: 10/Nov/2031
    */
    const char *verifyFilePath = "DigiCertHighAssuranceEVRootCA.crt.pem";
    sslContext->load_verify_file(verifyFilePath, error);
    if (error) {
        std::cout << "[ERROR]: " << error.message() << '\n';
    }

    try {
        temp::Client client { context, sslContext };
        const char *accessTokenHost = "eu.battle.net";
        client.Connect(accessTokenHost);
        client.Write(request);
        client.ReadHeader();
        client.ReadBodyChucks();
        client.Close();

        std::string body = client.GetBody();
        rapidjson::Document reader;
        reader.Parse(body.data());
        auto token = reader["access_token"].GetString();
        const auto tokenType = reader["token_type"].GetString();
        const auto expires = reader["expires_in"].GetUint64();

        [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
        assert(expires >= expectedDuration && "Unexpected duration. Blizzard API may be changed!");
        assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Blizzard API may be changed!");

        std::cout << "Extracted token: [" << token << "]\n";

        // WORK with token
        using namespace std::literals;
        request =   
        "GET /data/wow/token/?namespace=dynamic-eu&locale=en_US HTTP/1.1\r\n"
        "Host: eu.api.blizzard.com\r\n"
        "Authorization: Bearer "s + token + "\r\n\r\n";

        // temp::Client client2 { context, sslContext };
        const char *apiHost = "eu.api.blizzard.com";
        client.Connect(apiHost);
        
        client.Write(request);
        client.ReadHeader();
        if (client.IsEncoded()) {
            client.ReadBodyChucks();
        }
        else {
            client.ReadBody();
        }
        
        client.Write(request);
        client.ReadHeader();
        if (client.IsEncoded()) {
            client.ReadBodyChucks();
        }
        else {
            client.ReadBody();
        }
        client.Close();
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}