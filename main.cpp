// #include "Client.hpp"
// int main() {
//     auto context = std::make_shared<boost::asio::io_context>();
//     std::shared_ptr<Client> client = std::make_shared<Client>(context);
//     client->Run();
//     return 0;
// }

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <charconv>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

int main() {
    std::string request = 
    "POST /oauth/token HTTP/1.1\r\n"
    "Host: eu.battle.net\r\n"                
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Authorization: Basic YTA3YmI5MGE5OTAxNGRlMjkxNjdiNDRhNzJlN2NhMzY6ZnJERTR1SnVaeWM0bXo1RU9heWxlMmROSm8xQksxM08=\r\n"
    "Content-Length: 29\r\n"
    "\r\n"
    "grant_type=client_credentials";

    std::cout << request << '\n';

    boost::system::error_code error;
    boost::asio::io_context io_context;
    ssl::context ssl_context { ssl::context::method::sslv23_client };
    /**
     * [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
     * Cert Chain:
     * DigiCert High Assurance EV Root CA => DigiCert SHA2 High Assurance Server CA => *.battle.net
     * So root cert is DigiCert High Assurance EV Root CA;
     * Valid until: 10/Nov/2031
    */
    const char *verifyFilePath = "DigiCertHighAssuranceEVRootCA.crt.pem";
    ssl_context.load_verify_file(verifyFilePath, error);
    if (error) {
        std::cout << "[ERROR]: " << error.message() << '\n';
    }

    ssl::stream<tcp::socket> socket(io_context, ssl_context);
    tcp::resolver resolver(io_context);

    const char *host = "eu.battle.net";
    const auto endpoints = resolver.resolve(host, "https");
    if (endpoints.empty()) {
        std::cout << "[ERROR]: Endpoints not found!\n";
        std::abort();
    }

    error.clear();
    boost::asio::connect(socket.lowest_layer(), endpoints.begin(), endpoints.end(), error);
    if (error) {
        std::cout << "[ERROR]: " << error.message() << '\n';
        std::abort();
    }
    socket.lowest_layer().set_option(tcp::no_delay(true));

    try {
        // Perform SSL handshake and verify the remote host's certificate.
        socket.set_verify_mode(ssl::verify_peer);
        socket.set_verify_callback(ssl::rfc2818_verification(host));
        socket.handshake(ssl::stream<tcp::socket>::client);

        // ... read and write as normal ...
        boost::asio::write(socket, boost::asio::const_buffer(request.c_str(), request.size()));

        boost::asio::streambuf response;
        error.clear();

        {   
            constexpr std::string_view delimiter = "\r\n\r\n";
            const size_t headerLength = boost::asio::read_until(socket, response, delimiter, error);
            const auto data { response.data() };
            const std::string header {
                boost::asio::buffers_begin(data), 
                boost::asio::buffers_begin(data) + headerLength - delimiter.size()
            };
            response.consume(headerLength);
            std::cout << "\tHeader:\n" << header << '\n';
        }

        if (error) {
            throw boost::system::system_error(error);
        }

        constexpr std::string_view CRLF = "\r\n";
        std::string body = "";
        size_t chunkCounter = 0;
        for (size_t chunkCounter = 0, chunkLength = 0; 
            !error; // expect either EOF either other error to finish this loop
            chunkLength = boost::asio::read_until(socket, response, CRLF, error)
        ) {
            const auto data { response.data() };
            const auto size { response.size() };
            const std::string buffer {
                boost::asio::buffers_begin(data), 
                boost::asio::buffers_begin(data) + size
            };
            std::string_view bufferView { buffer };

            size_t proccessedBytes = 0;
            while (proccessedBytes < size) {
                auto chunk { bufferView };
                auto delimiter = chunk.find_first_of(CRLF);
                if (delimiter == std::string_view::npos) break;
                chunk.remove_suffix(chunk.size() - delimiter);
                bufferView.remove_prefix(delimiter + CRLF.size());
                proccessedBytes += delimiter + CRLF.size();
                if (chunkCounter & 1) {
                    std::cout << "Chunk " << chunkCounter / 2 << ':' << chunk << '\n';
                    body.append(chunk.data(), chunk.size());
                }
                else {
                    size_t value = 0;
                    auto [p, ec] = std::from_chars(std::data(chunk), std::data(chunk) + std::size(chunk), value, 16);
                    if (ec == std::errc()) {
                        std::cout << "Chunk " << chunkCounter / 2 << " size: " << value << '\n';
                    }
                    else {
                        std::cout << "[ERROR]: Unexpected input.\n\tBefore parsing: " 
                            << chunk << "\n\tAfter parsing: " << p << '\n';
                        std::abort();
                    }
                }
                chunkCounter++;
            }

            response.consume(proccessedBytes);
        }
        if (error != boost::asio::error::eof) {
            throw boost::system::system_error(error);
        }

        std::cout << "\tBody:\n" << body << "\n";
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}