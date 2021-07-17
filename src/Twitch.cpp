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
    auto connection = std::make_shared<Connection>(context_, ssl_, kId, kHost);

    connection->Write(request, [self = weak_from_this()
        , callback = std::move(continuation)
        , connection = connection->weak_from_this()
    ]() mutable {
        if (auto origin = connection.lock(); origin) {
            auto [head, body] = origin->AcquireResponse();
            rapidjson::Document reader; 
            reader.Parse(body.data(), body.size());
            /*
                {
                    "access_token": "<user access token>",
                    "refresh_token": "",
                    "expires_in": <number of seconds until the token expires>,
                    "scope": ["<your previously listed scope(s)>"],
                    "token_type": "bearer"
                }
            */
            std::string token = reader["access_token"].GetString();
            const auto tokenType = reader["token_type"].GetString();
            const auto expires = reader["expires_in"].GetUint64();

            [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
            assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Twitch API may be changed!");

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