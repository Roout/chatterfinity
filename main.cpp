#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <exception>
#include <mutex>
#include <chrono>

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

    namespace chrono = std::chrono;

    class Token {
    public:
        using TimePoint_t = chrono::time_point<chrono::steady_clock>;
        using Duration_t = chrono::seconds;

        Token() 
            : content_ {}
            , update_ { chrono::steady_clock::now() }
            , duration_ { 0 }
        {}

        Token(std::string token, Duration_t expireTime) 
            : content_ { std::move(token) }
            , update_ { chrono::steady_clock::now() }
            , duration_ { expireTime }
        {}

        void Emplace(std::string token, Duration_t expireTime) noexcept {
            std::lock_guard<std::mutex> lock{ mutex_ };
            content_ = std::move(token);
            update_ = chrono::steady_clock::now();
            duration_ = expireTime;
        }

        bool IsValid() const noexcept {
            const auto now = chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock{ mutex_ };
            return chrono::duration_cast<Duration_t>(now - update_) < duration_;
        }

        std::string Get() const {
            std::lock_guard<std::mutex> lock{ mutex_ };
            return content_;
        }

    private:

        std::string content_ {};
        TimePoint_t update_ { chrono::steady_clock::now() };
        Duration_t  duration_ { 0 };
        mutable std::mutex  mutex_;
    };

    class Blizzard : public std::enable_shared_from_this<Blizzard> {
    public:
        Blizzard(std::shared_ptr<ssl::context> ssl) 
            : m_context { std::make_shared<boost::asio::io_context>() }
            , m_sslContext { ssl }
            , m_work { m_context->get_executor() }
        {
        }

        Blizzard(const Blizzard&) = delete;
        Blizzard(Blizzard&&) = delete;
        Blizzard& operator=(const Blizzard&) = delete;
        Blizzard& operator=(Blizzard&&) = delete;

        ~Blizzard() {
            std::cout << "dtor!\n";
        }

        void Run() {
            std::vector<std::thread> threads;
            for (size_t i = 0; i < kThreads; i++) {
                threads.emplace_back([this](){
                    for (;;) {
                        try {
                            m_context->run();
                            break;
                        }
                        catch (std::exception& ex) {
                            // some system exception
                            std::lock_guard<std::mutex> lock { m_outputMutex };
                            std::cerr << "[ERROR]: " << ex.what() << '\n';
                        }
                    }
                });
            }
            for (auto&& t: threads) t.join();
        }

        void AcquireToken(std::function<void()> continuation = {}) {
            std::string request = blizzard::CredentialsExchange(
                "a07bb90a99014de29167b44a72e7ca36", "frDE4uJuZyc4mz5EOayle2dNJo1BK13O"
            ).Build();
            const char *accessTokenHost = "eu.battle.net";
            const size_t id = 0;
            auto connection = std::make_shared<Connection>(m_context, m_sslContext, id, accessTokenHost);

            auto weak = weak_from_this();
            const auto count = weak.use_count();


            connection->Write(request, [self = weak
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    auto [head, body] = origin->AcquireResponse();
                    rapidjson::Document reader; 
                    reader.Parse(body.data(), body.size());
                    
                    std::string token = reader["access_token"].GetString();
                    const auto tokenType = reader["token_type"].GetString();
                    const auto expires = reader["expires_in"].GetUint64();

                    [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
                    assert(expires >= expectedDuration && "Unexpected duration. Blizzard API may be changed!");
                    assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Blizzard API may be changed!");

                    if (auto service = self.lock(); service) {
                        // TODO: remove this mutex and print to log file
                        {
                            std::lock_guard<std::mutex> lock { service->m_outputMutex };
                            std::cout << "Extracted token: [" << token << "]\n";
                        }
                        service->m_token.Emplace(std::move(token), Token::Duration_t(expires));

                        if (callback) { // TODO: Decide whether to abandon this approach and use observer pattern for token!
                            boost::asio::post(*service->m_context, callback);
                        }
                    }
                }
                else {
                    assert(false && "Unreachable. For now you can invoke this function only synchroniously");
                }
            });

        }

    private:
        using Work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        static constexpr size_t kThreads { 2 };
        mutable std::mutex m_outputMutex;
        Token m_token;
        std::shared_ptr<boost::asio::io_context> m_context;
        std::shared_ptr<ssl::context> m_sslContext;
        Work_t m_work;
    };
}

int main() {
    boost::system::error_code error;
    // auto context = std::make_shared<boost::asio::io_context>();
    auto sslContext = std::make_shared<ssl::context>(ssl::context::method::sslv23_client);
    auto blizzardService = std::make_shared<temp::Blizzard>(sslContext);
    // auto work = boost::asio::executor_work_guard(context->get_executor());

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

    // // WORK with token
    // auto req = blizzard::Realm(token).Build();  
    // const char *apiHost = "eu.api.blizzard.com";

    try {
        blizzardService->AcquireToken([](){
            std::cout << "Fuck!\n";
        });
        blizzardService->Run();
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}