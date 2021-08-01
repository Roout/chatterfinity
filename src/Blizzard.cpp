#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Request.hpp"
#include "Connection.hpp"

#include <exception>

#include "rapidjson/document.h"

namespace {

struct RealmStatusWrapper final {
    std::string name;
    std::string queue;
    std::string status;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());
        if (!json.HasMember("realms")) return false;
        const auto realms = json["realms"].GetArray();
        assert(!realms.Empty() && "Empty realms");

        // TODO: don't take just first value
        const auto& front = *realms.Begin();
        if (!front.HasMember("name")) return false;
        name = front["name"].GetString();

        if (!json.HasMember("has_queue")) return false;
        const auto hasQueue = json["has_queue"].GetBool();
        queue = hasQueue? "has queue": "no queue";

        if (!json.HasMember("status") || !json["status"].HasMember("type")) return false;
        status = json["status"]["type"].GetString();

        return true;
    }

};

std::string to_string(const RealmStatusWrapper& realm) {
    return realm.name + "(" + realm.status + "): " + realm.queue;
}


struct TokenWrapper final {
    std::string content;
    std::string type;
    std::uint64_t expires;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());

        if (auto it = json.FindMember("access_token"); it != json.MemberEnd()) {
            content = it->value.GetString();
        }
        else {
            return false;
        }

        if (auto it = json.FindMember("token_type"); it != json.MemberEnd()) {
            type = it->value.GetString();
        }
        else {
            return false;
        }

        if (auto it = json.FindMember("expires_in"); it != json.MemberEnd()) {
            expires = it->value.GetUint64();
        }
        else {
            return false;
        }

        assert(type == "bearer" 
            && "Unexpected token type. Blizzard API may be changed!");

        return true;
    }

};

std::string to_string(const TokenWrapper& token) {
    return token.content;
}

} // namespace {

namespace service {

Blizzard::Blizzard(const Config *config, Container * outbox) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
    , outbox_ { outbox }
{
    assert(config_ && "Config is NULL");

    const char * const kVerifyFilePath = "crt/DigiCertHighAssuranceEVRootCA.crt.pem";
    // [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
    // TODO: read this path from secret + with some chiper
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[blizzard] [ERROR]:", error.message(), '\n');
    }
}

Blizzard::~Blizzard() {
    Console::Write("  -> close blizzard service\n");
    // TODO: 
    // - [ ] close io_context? context_->stop();
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

void Blizzard::QueryRealm(std::function<void(size_t realmId)> continuation) {
    constexpr char * const kHost { "eu.api.blizzard.com" };
    constexpr char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );
    auto onConnect = [request = blizzard::Realm(token_.Get()).Build()
        , service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1 && 
            "Fail invariant:"
            "Expected: 1 ref - instance which is executing Connection::OnWrite"
            "Assertion Failure may be caused by changing the "
            "(way)|(place where) this callback is being invoked"
        );
        auto shared = connection.lock();
        shared->ScheduleWrite(std::move(request), [service
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1);

            auto OnReadSuccess = [service
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);
                
                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();
                rapidjson::Document json; 
                json.Parse(body.data(), body.size());
                const auto realmId = json["id"].GetUint64();
                Console::Write("[blizzard] realm id: [", realmId, "]\n");
                if (callback) {
                    boost::asio::post(*service->context_, std::bind(callback, realmId));
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::QueryRealmStatus(size_t realmId, command::RealmStatus cmd, std::function<void()> continuation) {
    constexpr char * const kHost { "eu.api.blizzard.com" };
    constexpr char * const kService { "https" };
    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    auto onConnect = [request = blizzard::RealmStatus(realmId, token_.Get()).Build()
        , cmd = std::move(cmd)
        , service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1 && 
            "Fail invariant:"
            "Expected: 1 ref - instance which is executing Connection::OnWrite"
            "Assertion Failure may be caused by changing the "
            "(way)|(place where) this callback is being invoked"
        );

        auto shared = connection.lock();
        shared->ScheduleWrite(std::move(request), [service
            , cmd = std::move(cmd)
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1);

            auto OnReadSuccess = [service
                , cmd = std::move(cmd)
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);

                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();

                RealmStatusWrapper realm;
                std::string message {""};
                if (!realm.Parse(body)) {
                    message = "sorry, can't provide the answer. Try later please!";
                    Console::Write("[blizzard] can't parse response: [", body, "]\n");
                }
                else {
                    message = to_string(realm);
                }
                
                // TODO: update this temporary solution base on IF
                if (cmd.initiator_.empty()) { // the sourceof the command is 
                    Console::Write("[blizzard] recv:", message, '\n');
                }
                else {
                    message = "@" + cmd.initiator_ + ", " + message;
                    command::RawCommand raw { "chat", { std::move(cmd.channel_), std::move(message) } };
                    if (!service->outbox_->TryPush(std::move(raw))) {
                        Console::Write("[blizzard] fail to push realm-status response to queue: is full\n");
                    }
                }
               
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::AcquireToken(std::function<void()> continuation) {
    constexpr char * const kHost { "eu.battle.net" };
    constexpr char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    auto onConnect = [service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1);
        
        const Config::Identity identity { "blizzard" };
        const auto secret = service->GetConfig()->GetSecret(identity);
        if (!secret) { 
            throw std::exception("Cannot find a service with identity = blizzard");
        }
        auto request = blizzard::CredentialsExchange(secret->id_, secret->secret_).Build();
        auto shared = connection.lock();

        shared->ScheduleWrite(std::move(request), [service
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1 && 
                "Fail invariant:"
                "Expected: 1 ref - instance which is executing Connection::OnWrite"
                "Assertion Failure may be caused by changing the "
                "(way)|(place where) this callback is being invoked"
            );

            auto OnReadSuccess = [service
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);

                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();
                
                TokenWrapper token;
                token.Parse(body);

                Console::Write("[blizzard] extracted token: [", to_string(token), "]\n");
                service->token_.Emplace(std::move(token.content), AccessToken::Duration(token.expires));
                
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::Invoker::Execute(command::RealmID) {
    auto initiateRealmQuery = [blizzard = blizzard_]() {
        blizzard->QueryRealm([](size_t realmId) {
            Console::Write("[blizzard] acquire realm id:", realmId, '\n');
        });
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateRealmQuery));
    }
    else {
        std::invoke(initiateRealmQuery);
    }
}

void Blizzard::Invoker::Execute(command::Arena arena) {
    auto arenaQuery = [blizzard = blizzard_, cmd = std::move(arena)]() {
        Console::Write("[blizzard] arena: [ initiator =",
             cmd.initiator_, ", channel =", cmd.channel_, "]\n");
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(arenaQuery));
    }
    else {
        std::invoke(arenaQuery);
    }
}

void Blizzard::Invoker::Execute(command::RealmStatus cmd) {
    auto initiateQuery = [blizzard = blizzard_, cmd = std::move(cmd)]() {
        blizzard->QueryRealm([blizzard, cmd = std::move(cmd)](size_t realmId) {
            Console::Write("[blizzard] acquire realm id:", realmId, '\n');
            blizzard->QueryRealmStatus(realmId, std::move(cmd), []() {
                Console::Write("[blizzard] complete realm status request\n");
            });
        });
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateQuery));
    }
    else {
        std::invoke(initiateQuery);
    }
}

void Blizzard::Invoker::Execute(command::AccessToken) {
    blizzard_->AcquireToken([]() {
        Console::Write("[blizzard] token acquired.\n");
    });
}


} // service