#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Request.hpp"
#include "Connection.hpp"

#include <exception>

#include <boost/format.hpp>

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

    const char * const kVerifyFilePath = "crt/DigiCertHighAssuranceEVRootCA.crt.pem";
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
    // TODO: 
    // - [ ] close io_context? context_->stop();
    // - [ ] close ssl context? 
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

    auto connection = std::make_shared<HttpConnection>(context_
        , ssl_
        , (boost::format("%1%_%2%_%3%.txt") % kHost % kService % GenerateId()).str()
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
        shared->Write(request, [service
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
                Console::Write("Realm id: [", realmId, "]\n");
                if (callback) {
                    boost::asio::post(*service->context_, std::bind(callback, realmId));
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(kHost, kService, std::move(onConnect));
}

void Blizzard::QueryRealmStatus(size_t realmId, std::function<void()> continuation) {
    constexpr char * const kHost { "eu.api.blizzard.com" };
    constexpr char * const kService { "https" };
    auto connection = std::make_shared<HttpConnection>(context_
        , ssl_
        , (boost::format("%1%_%2%_%3%.txt") % kHost % kService % GenerateId()).str()
    );
    // TODO: remove token => use config!
    auto onConnect = [request = blizzard::RealmStatus(realmId, token_.Get()).Build()
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
        shared->Write(request, [service
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
                const auto realms = json["realms"].GetArray();
                assert(!realms.Empty());    
                const auto& front = *realms.Begin();
                const std::string name = front["name"].GetString();
                const auto hasQueue = json["has_queue"].GetBool();
                const std::string status = json["status"]["type"].GetString();

                Console::Write(name, "(", status, "):", hasQueue? "\"has queue\"": "\"no queue\"\n");
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(kHost, kService, std::move(onConnect));
}

void Blizzard::AcquireToken(std::function<void()> continuation) {
    constexpr char * const kHost { "eu.battle.net" };
    constexpr char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(context_
        , ssl_
        , (boost::format("%1%_%2%_%3%.txt") % kHost % kService % GenerateId()).str()
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

        shared->Write(request, [service
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
                rapidjson::Document json; 
                json.Parse(body.data(), body.size());
                std::string token = json["access_token"].GetString();
                const auto tokenType = json["token_type"].GetString();
                const auto expires = json["expires_in"].GetUint64();

                [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
                assert(expires >= expectedDuration && "Unexpected duration. Blizzard API may be changed!");
                assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Blizzard API may be changed!");

                Console::Write("Extracted token: [", token, "]\n");
                service->token_.Emplace(std::move(token), AccessToken::Duration(expires));
                
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(kHost, kService, std::move(onConnect));
}

void Blizzard::Invoker::Execute(command::RealmID) {
    auto initiateRealmQuery = [blizzard = blizzard_]() {
        blizzard->QueryRealm([](size_t realmId) {
            Console::Write("ID acquired:", realmId, '\n');
        });
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateRealmQuery));
    }
    else {
        std::invoke(initiateRealmQuery);
    }
}

void Blizzard::Invoker::Execute(command::RealmStatus) {
    auto initiateQuery = [blizzard = blizzard_]() {
        blizzard->QueryRealm([blizzard](size_t realmId) {
            Console::Write("ID acquired: ", realmId, '\n');
            blizzard->QueryRealmStatus(realmId, []() {
                Console::Write("Realm confirmed!\n");
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
        Console::Write("Token acquired.\n");
    });
}


} // service