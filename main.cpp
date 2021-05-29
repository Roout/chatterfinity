// #include "Client.hpp"
// int main() {
//     auto context = std::make_shared<boost::asio::io_context>();
//     std::shared_ptr<Client> client = std::make_shared<Client>(context);
//     client->Run();
//     return 0;
// }

#include <iostream>
#include <string>
#include <vector>
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
        exit(1);
    }

    error.clear();
    boost::asio::connect(socket.lowest_layer(), endpoints.begin(), endpoints.end(), error);
    if (error) {
        std::cout << "[ERROR]: " << error.message() << '\n';
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
        std::cout << "\n\tRead: \n";
        while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error)) {
            std::cout << &response;
        }

        if (error != boost::asio::error::eof)
            throw boost::system::system_error(error);
    
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}