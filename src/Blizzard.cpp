#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Request.hpp"

#include <exception>

#include "rapidjson/document.h"

namespace service {

Blizzard::Blizzard(const Config *config) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
{
    assert(config_ && "Config is NULL");

    const char * const kVerifyFilePath = "DigiCertHighAssuranceEVRootCA.crt.pem";
    /**
     * [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
     * Cert Chain:
     * ```
     * DigiCert High Assurance EV Root CA 
     *  => DigiCert SHA2 High Assurance Server CA 
     *  => *.battle.net
     * ```
     * So root cert is DigiCert High Assurance EV Root CA;
     * Valid until: 10/Nov/2031
     * 
     * TODO: read this path from secret + with some chiper
    */
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[ERROR]: ", error.message(), '\n');
    }
}

Blizzard::~Blizzard() {
    Console::Write("  -> close blizzard service\n");
    for (auto& t: threads_) t.join();
}

void Blizzard::ResetWork() {
    work_.reset();
}

void Blizzard::Run() {
    for (size_t i = 0; i < kThreads; i++) {
        threads_.emplace_back([this](){
            for (;;) {
                try {
                    context_->run();
                    break;
                }
                catch (std::exception& ex) {
                    // some system exception
                    Console::Write("[ERROR]: ", ex.what(), '\n');
                }
            }
        });
    }
}

void Blizzard::QueryRealm(std::function<void(size_t realmId)> continuation) const {
    const char * const kHost = "eu.api.blizzard.com";
    auto request = blizzard::Realm(token_.Get()).Build();
    auto connection = std::make_shared<Connection>(context_, ssl_, GenerateId(), kHost);

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
                Console::Write("Realm id: [", realmId, "]\n");
                if (callback) {
                    boost::asio::post(*service->context_, std::bind(callback, realmId));
                }
            }
        }
        else {
            assert(false && "Unreachable. For now you can invoke this function only synchroniously");
        }
    });
}

void Blizzard::QueryRealmStatus(size_t realmId, std::function<void()> continuation) const {
    constexpr char * const kHost = "eu.api.blizzard.com";
    auto request = blizzard::RealmStatus(realmId, token_.Get()).Build();
    auto connection = std::make_shared<Connection>(context_, ssl_, GenerateId(), kHost);

    connection->Write(request, [self = weak_from_this()
        , callback = std::move(continuation)
        , connection = connection->weak_from_this()
    ]() mutable {
        if (auto origin = connection.lock(); origin) {
            const auto [head, body] = origin->AcquireResponse();

            if (auto service = self.lock(); service) {
                rapidjson::Document reader; 
                reader.Parse(body.data(), body.size());
                const auto realms = reader["realms"].GetArray();
                assert(!realms.Empty());    
                const auto& front = *realms.Begin();
                const std::string name = front["name"].GetString();
                const auto hasQueue = reader["has_queue"].GetBool();
                const std::string status = reader["status"]["type"].GetString();

                Console::Write(name, "(", status, "):", hasQueue? "\"has queue\"": "\"no queue\"\n");

                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            }
        }
        else {
            assert(false && "Unreachable. For now you can invoke this function only synchroniously");
        }
    });
}

void Blizzard::AcquireToken(std::function<void()> continuation) {
    const Config::Identity identity { "blizzard"};
    const auto secret = config_->GetSecret(identity);
    if (!secret) { 
        throw std::exception("Cannot find a service with identity = blizzard");
    }

    auto request = blizzard::CredentialsExchange(secret->id_, secret->value_).Build();

    constexpr char * const kHost = "eu.battle.net";
    auto connection = std::make_shared<Connection>(context_, ssl_, GenerateId(), kHost);

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
                Console::Write("Extracted token: [", token, "]\n");
                service->token_.Emplace(std::move(token), AccessToken::Duration(expires));

                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            }
        }
        else {
            assert(false && "Unreachable. For now you can invoke this function only synchroniously");
        }
    });

}

void Blizzard::Invoker::Execute(command::RealmID) {
    auto initiateRealmQuery = [weak = blizzard_->weak_from_this()]() {
        if (auto self = weak.lock(); self) {
            self->QueryRealm([weak](size_t realmId) {
                if (auto self = weak.lock(); self) {
                    Console::Write("ID acquired:", realmId, '\n');
                }
            });
        }
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateRealmQuery));
    }
    else {
        std::invoke(initiateRealmQuery);
    }
}

void Blizzard::Invoker::Execute(command::RealmStatus) {
    auto initiateRealmQuery = [weak = blizzard_->weak_from_this()]() {
            if (auto self = weak.lock(); self) {
                self->QueryRealm([weak](size_t realmId) {
                    if (auto self = weak.lock(); self) {
                        Console::Write("ID acquired: ", realmId, '\n');
                        self->QueryRealmStatus(realmId, [weak]() {
                            if (auto self = weak.lock(); self) {
                                Console::Write("Realm confirmed!\n");
                            }
                        });
                    }
                });
            }
        };
        if (!blizzard_->token_.IsValid()) {
            blizzard_->AcquireToken(std::move(initiateRealmQuery));
        }
        else {
            std::invoke(initiateRealmQuery);
        }
}

void Blizzard::Invoker::Execute(command::AccessToken) {
    blizzard_->AcquireToken([weak = blizzard_->weak_from_this()]() {
        if (auto self = weak.lock(); self) {
            Console::Write("Token acquired.\n");
        }
    });
}


} // service