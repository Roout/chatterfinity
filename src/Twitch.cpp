#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"

#include "rapidjson/document.h"

namespace service {

Twitch::Twitch(const Config *config) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
{
    assert(config_ && "Config is NULL");
    /**
     * [Amazon CA](https://www.amazontrust.com/repository/)
     * Distinguished Name:
     * ```
     * CN=Starfield Services Root Certificate Authority - G2,
     * O=Starfield Technologies\, Inc.,
     * L=Scottsdale,
     * ST=Arizona,
     * C=US
     * ```
     * TODO: read this path from secret + with some chiper
    */
    const char * const kVerifyFilePath = "StarfieldServicesRootCA.crt.pem";
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[ERROR]: ", error.message(), '\n');
    }
}

Twitch::~Twitch() {
    Console::Write("  -> close twitch service\n");
    for (auto& t: threads_) t.join();
}

void Twitch::ResetWork() {
    work_.reset();
}

void Twitch::Run() {
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

void Twitch::AcquireToken(std::function<void()> continuation) {
    constexpr char * const kHost { "id.twitch.tv" };
    constexpr size_t kId { 0 };
    const Config::Identity identity { "twitch" };

    const auto secret = config_->GetSecret(identity);
    if (!secret) { 
        throw std::exception("Cannot find a service with identity = twitch");
    }

    auto request = twitch::CredentialsExchange(secret->id_, secret->value_).Build();
    auto connection = std::make_shared<HttpConnection>(context_, ssl_, kId, kHost);
    // TODO: 
    // Q: Can connection outlive the service? 
    // A: Shouldn't (service can't be destroyed while the working threads are running. 
    //  Joining threads means context is not running so connection is nowhere to be stored => ref_count == 0)
    // Q: 
    // A: 
    connection->Write(request, [service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
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
            assert(connection.use_count() == 1 && 
                "Fail invariant:"
                "Expected: 1 ref - instance which is executing Connection::*"
                "Assertion Failure may be caused by changing the "
                "(way)|(place where) this callback is being invoked"
            );
            auto shared = connection.lock();
            const auto [head, body] = shared->AcquireResponse();
            rapidjson::Document json; 
            json.Parse(body.data(), body.size());

            std::string token = json["access_token"].GetString();
            const auto tokenType = json["token_type"].GetString();
            const auto expires = json["expires_in"].GetUint64();

            [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
            assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Twitch API may be changed!");

            Console::Write("Extracted token: [", token, "]\n");
            service->token_.Emplace(std::move(token), AccessToken::Duration(expires));

            if (callback) {
                boost::asio::post(*service->context_, callback);
            }
        };

        auto shared = connection.lock();
        // call read operation
        shared->Read(std::move(OnReadSuccess));
    });
}

void Twitch::Invoker::Execute(command::AccessToken) {
    twitch_->AcquireToken([weak = twitch_->weak_from_this()]() {
        if (auto self = weak.lock(); self) {
            Console::Write("Token acquired.\n");
        }
    });
}

void Twitch::Invoker::Execute(command::Help) {

}

void Twitch::Invoker::Execute(command::Shutdown) {

}



} // namespace service