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
#include "Token.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

namespace temp {

    struct Config {
        std::string id_;
        std::string secret_;

        static constexpr std::string_view kConfigPath = "secret/secrets.cfg";
        static constexpr char kDelimiter = ':';

    public:
        void Read() {
            std::ifstream in(kConfigPath.data());
            if (!in.is_open()) {
                throw std::exception("Failed to open config file");
            }
            std::string buffer;
            while (getline(in, buffer)) {
                const auto delimiter = buffer.find(kDelimiter);
                if (delimiter == std::string::npos) {
                    throw std::exception("Wrong config file format");
                }

                std::string_view key { buffer.data(), delimiter };
                key = utils::Trim(key, " \"\n");
                std::string_view value { buffer.data() + delimiter + 1 };
                value = utils::Trim(value, " \"\n");
                
                if (utils::IsEqual(key, "id")) {
                    id_.assign(value.data(), value.size());
                }
                else if (utils::IsEqual(key, "secret")) {
                    secret_.assign(value.data(), value.size());
                }
                else {
                    assert(false && "Unreachable: unexcpected key");
                }
            }
            assert(!id_.empty());
            assert(!secret_.empty());
        }

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
            std::lock_guard<std::mutex> lock { m_outputMutex };
            std::cout << "~Blizzard()\n";
        }

        void ResetWork() {
            m_work.reset();
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

        void QueryRealm(std::function<void(size_t realmId)> continuation = {}) const {
            const char * const kHost = "eu.api.blizzard.com";
            auto request = blizzard::Realm(m_token.Get()).Build();
            auto connection = std::make_shared<Connection>(m_context, m_sslContext, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    const auto [head, body] = origin->AcquireResponse();
                    rapidjson::Document reader; 
                    reader.Parse(body.data(), body.size());
                    const auto realmId = reader["id"].GetUint64();

                    if (auto service = self.lock(); service) {
                        // TODO: remove this mutex and print to log file
                        {
                            std::lock_guard<std::mutex> lock { service->m_outputMutex };
                            std::cout << "Realm id: [" << realmId << "]\n";
                        }
                        if (callback) { // TODO: Decide whether to abandon this approach and use observer pattern for token!
                            boost::asio::post(*service->m_context, std::bind(callback, realmId));
                        }
                    }
                }
                else {
                    assert(false && "Unreachable. For now you can invoke this function only synchroniously");
                }
            });
        }

        void QueryRealmStatus(size_t realmId, std::function<void()> continuation = {}) const {
            constexpr char * const kHost = "eu.api.blizzard.com";
            auto request = blizzard::RealmStatus(realmId, m_token.Get()).Build();
            auto connection = std::make_shared<Connection>(m_context, m_sslContext, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    const auto [head, body] = origin->AcquireResponse();

                    if (auto service = self.lock(); service) {
                        // TODO: remove this mutex and print to log file
                        {
                            std::lock_guard<std::mutex> lock { service->m_outputMutex };
                            std::cout << "Body:\n" << body << "\n";
                        }
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

        void AcquireToken(std::function<void()> continuation = {}) {
            m_config.Read();
            auto request = blizzard::CredentialsExchange(m_config.id_, m_config.secret_).Build();

            constexpr char * const kHost = "eu.battle.net";
            auto connection = std::make_shared<Connection>(m_context, m_sslContext, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
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
        
        size_t GenerateId() const {
            return Blizzard::lastId++;
        }


        static constexpr size_t kThreads { 2 };
        mutable std::mutex m_outputMutex;
        Token m_token;
        std::shared_ptr<boost::asio::io_context> m_context;
        std::shared_ptr<ssl::context> m_sslContext;
        Work_t m_work;

        Config m_config;

        // connection id
        static inline size_t lastId { 0 };
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

    try {
        blizzardService->AcquireToken([blizzardService]() {
            blizzardService->QueryRealm([blizzardService](size_t realmId) {
                blizzardService->QueryRealmStatus(realmId, [blizzardService](){
                    blizzardService->ResetWork();
                });
            });
        });
        blizzardService->Run();
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}