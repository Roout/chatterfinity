#include "Blizzard.hpp"
#include "Console.hpp"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

Blizzard::Blizzard(std::shared_ptr<ssl::context> ssl) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , sslContext_ { ssl }
    , invoker_ { std::make_unique<Invoker>(this) }
{
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
    auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

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
    auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

    connection->Write(request, [self = weak_from_this()
        , callback = std::move(continuation)
        , connection = connection->weak_from_this()
    ]() mutable {
        if (auto origin = connection.lock(); origin) {
            const auto [head, body] = origin->AcquireResponse();

            if (auto service = self.lock(); service) {
                Console::Write("Body:\n", body, "\n");
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
    config_.Read();
    auto request = blizzard::CredentialsExchange(config_.id_, config_.secret_).Build();

    constexpr char * const kHost = "eu.battle.net";
    auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

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
                service->token_.Emplace(std::move(token), Token::Duration(expires));

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
