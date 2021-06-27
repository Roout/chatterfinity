#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <exception>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <iterator>

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
#include "Config.hpp"
#include "Command.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;


namespace temp {
    // service
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
            {
                std::lock_guard<std::mutex> lock { m_outputMutex };
                std::cout << "~Blizzard()\n";
            }
            for (auto& t: m_threads) t.join();
        }

        template<typename Command,
            typename = std::enable_if_t<command::is_blizzard_api<Command>::value>
        >
        void Execute(Command& cmd) {
            if constexpr (std::is_same_v<Command, command::RealmID>) {
                auto initiateRealmQuery = [weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        self->QueryRealm([weak](size_t realmId) {
                            if (auto self = weak.lock(); self) {
                                std::cout << "ID acquired: " << realmId << '\n';
                            }
                        });
                    }
                };
                if (!m_token.IsValid()) {
                    AcquireToken(std::move(initiateRealmQuery));
                }
                else {
                    std::invoke(initiateRealmQuery);
                }
            }
            else if (std::is_same_v<Command, command::RealmStatus>) {
                auto initiateRealmQuery = [weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        self->QueryRealm([weak](size_t realmId) {
                            if (auto self = weak.lock(); self) {
                                std::cout << "ID acquired: " << realmId << '\n';
                                self->QueryRealmStatus(realmId, [weak](){
                                    if (auto self = weak.lock(); self) {
                                        std::cout << "Realm confirmed!\n";
                                    }
                                });
                            }
                        });
                    }
                };
                if (!m_token.IsValid()) {
                    AcquireToken(std::move(initiateRealmQuery));
                }
                else {
                    std::invoke(initiateRealmQuery);
                }
            }
            else if (std::is_same_v<Command, command::AccessToken>) {
                AcquireToken([weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        std::cout << "Token acquired.\n";
                    }
                });
            }
            else {
                assert(false && "Unreachable: Unknown blizzard command");
            }
        }

        void ResetWork() {
            m_work.reset();
        }

        void Run() {
            for (size_t i = 0; i < kThreads; i++) {
                m_threads.emplace_back([this](){
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
                        if (callback) {
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
                        if (callback) {
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

                        if (callback) {
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
        std::vector<std::thread> m_threads;

        mutable std::mutex m_outputMutex;
        Token m_token;
        std::shared_ptr<boost::asio::io_context> m_context;
        std::shared_ptr<ssl::context> m_sslContext;
        Work_t m_work;

        Config m_config;

        // connection id
        static inline size_t lastId { 0 };
    };

    // This is source of input 
    class Console {
    public:
        Console(std::shared_ptr<Blizzard> blizzard) 
            : blizzard_ { blizzard }
        {
        }

        // blocks execution thread
        void Run() {
            using namespace std::literals;

            static constexpr std::string_view kDelimiter { " " };
            std::string buffer;
            while (true) {
                std::getline(std::cin, buffer);
                std::string_view input { buffer };
                input = utils::Trim(input);
                // parse buffer
                std::vector<std::string_view> args;
                auto delimiter = input.find(kDelimiter);
                while (delimiter != std::string_view::npos) {
                    args.emplace_back(input.data(), delimiter);
                    // remove prefix with delimiter
                    input.remove_prefix(delimiter + 1);
                    // remove all special characters from the input
                    input = utils::Trim(input);
                    // update delimiter position
                    delimiter = input.find(kDelimiter);
                }
                args.emplace_back(input);
                assert(!args.empty() && "Args list can't be empty");
                // pass command to executor
                if (args.front() == "!realm-id"sv) {
                    command::RealmID().accept(*blizzard_);
                }
                else if (args.front() == "!realm-status"sv) {
                    command::RealmStatus().accept(*blizzard_);
                }
                else if (args.front() == "!token"sv) {
                    command::AccessToken().accept(*blizzard_);
                }
                else if (args.front() == "!quit"sv) {
                    std::cout << "Bye!\n";
                    blizzard_->ResetWork();
                    break;
                }
                else {
                    std::cout << "\"{\nparsed\": [\n\t";
                    std::copy(args.cbegin(), args.cend(), std::ostream_iterator<std::string_view>(std::cout, ", "));
                    std::cout << "\n]}\n";
                }
            }
        }

    private:
        // blizzard API
        std::shared_ptr<Blizzard> blizzard_;
    };

}

int main() {
    boost::system::error_code error;
    auto sslContext = std::make_shared<ssl::context>(ssl::context::method::sslv23_client);
    auto blizzardService = std::make_shared<temp::Blizzard>(sslContext);

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
        blizzardService->Run();
        temp::Console(blizzardService).Run();
    }
    catch (std::exception const& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}