#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <charconv>
#include <unordered_map>
#include <stdexcept>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Response.hpp"
#include "Request.hpp"
#include "Utility.hpp"
#include "Connection.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

namespace temp {

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
      
    private:
        static constexpr std::string_view CRLF = "\r\n";

        std::shared_ptr<boost::asio::io_context> m_context;
        std::shared_ptr<ssl::context> m_sslContext;
    };
}

int main() {
    boost::system::error_code error;
    auto context = std::make_shared<boost::asio::io_context>();
    auto sslContext = std::make_shared<ssl::context>(ssl::context::method::sslv23_client);
    auto work = boost::asio::executor_work_guard(context->get_executor());

    /**
     * [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
     * Cert Chain:
     * DigiCert High Assurance EV Root CA => DigiCert SHA2 High Assurance Server CA => *.battle.net
     * So root cert is DigiCert High Assurance EV Root CA;
     * Valid until: 10/Nov/2031
     * 
     * TODO: read this path from secret + with some chiper
    */
    const char *verifyFilePath = "DigiCertHighAssuranceEVRootCA.crt.pem";
    sslContext->load_verify_file(verifyFilePath, error);
    if (error) {
        std::cout << "[ERROR]: " << error.message() << '\n';
    }

    try {
        std::string request = blizzard::CredentialsExchange(
            "a07bb90a99014de29167b44a72e7ca36", "frDE4uJuZyc4mz5EOayle2dNJo1BK13O"
        ).Build();
        temp::Client client { context, sslContext };
        const char *accessTokenHost = "eu.battle.net";
        const size_t id = 0;
        auto connection = std::make_shared<Connection>(context, sslContext, id, accessTokenHost);

        connection->Write(request, [connection]() mutable {
            auto [head, body] = connection->AcquireResponse();
            rapidjson::Document reader; 
            reader.Parse(body.data(), body.size());
            const auto token = reader["access_token"].GetString();
            const auto tokenType = reader["token_type"].GetString();
            const auto expires = reader["expires_in"].GetUint64();

            [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
            assert(expires >= expectedDuration && "Unexpected duration. Blizzard API may be changed!");
            assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Blizzard API may be changed!");

            std::cout << "Extracted token: [" << token << "]\n";

            // WORK with token
            auto req = blizzard::Realm(token).Build();  
            const char *apiHost = "eu.api.blizzard.com";
        });

        context->run();
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}