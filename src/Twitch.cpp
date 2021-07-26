#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"
#include "Command.hpp"
#include "Utility.hpp"

#include "rapidjson/document.h"

namespace service {

Twitch::Twitch(const Config *config) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , translator_ {}
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
    const char * const kVerifyFilePath = "crt/StarfieldServicesRootCA.crt.pem";
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[ERROR]: ", error.message(), '\n');
    }

    using namespace std::literals::string_view_literals;
    std::initializer_list<Translator::Pair> list {
        { "help"sv,     Translator::CreateHandle<command::Help>(*this) },
        { "ping"sv,     Translator::CreateHandle<command::Pong>(*this) }
    };
    translator_.Insert(list);
}

Twitch::~Twitch() {
    Console::Write("  -> close twitch service\n");
    for (auto& t: threads_) t.join();
}

void Twitch::ResetWork() {
    work_.reset();
    if (irc_) {
        irc_->Close();
        irc_.reset();
    }
    context_->stop();
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

void Twitch::HandleResponse(net::irc::Message message) {
    if (auto handle = translator_.GetHandle(utils::AsLowerCase(message.command_)); handle) {
        Console::Write("twitch: handle", message.command_, '\n');
        // proccess command here
        Translator::Params params;
        const size_t paramsCount { message.params_.size() };
        params.resize(paramsCount);
        for (size_t i = 0; i < paramsCount; i++) {
            params[i] = { message.params_[i].data(), message.params_[i].size() };
        }
        std::invoke(*handle, params);
    }
    else {
        std::string raw = message.prefix_ + ":" + message.command_ + ":";
        for(auto& p: message.params_) raw += p + " ";
        Console::Write("parse:", raw, '\n');
    }
}

void Twitch::Invoker::Execute(command::Help) {
    assert(false && "TODO: not implemented");
}

void Twitch::Invoker::Execute(command::Pong) {
    auto pongRequest = twitch::Pong{}.Build();
    assert(true && "TODO: Confirm that connection is alive"
        "after introducing connection state"
    );
    twitch_->irc_->Write(std::move(pongRequest), []() {
        Console::Write("send pong request\n");
    });
}

void Twitch::Invoker::Execute(command::Validate) {
    constexpr std::string_view kHost { "id.twitch.tv" };
    constexpr std::string_view kService { "https" };
    const size_t kId { 0 };

    auto connection = std::make_shared<HttpConnection>(
        twitch_->context_, twitch_->ssl_, kHost, kService, kId
    );

    const Config::Identity kIdentity { "twitch" };
    const auto secret = twitch_->GetConfig()->GetSecret(kIdentity);
    if (!secret) {
        throw std::exception("Fail to create config");
    }
    auto onConnect = [request = twitch::Validation{ secret->token_ }.Build()
        , twitchService = twitch_
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1 && 
            "Fail invariant:"
            "Expected: 1 ref - instance which is executing Connection::OnWrite"
            "Assertion Failure may be caused by changing the "
            "(way)|(place where) this callback is being invoked"
        );
        auto shared = connection.lock();
        shared->Write(request, [twitchService, connection]() {
            assert(connection.use_count() == 1);

            auto OnReadSuccess = [twitchService, connection]() {
                assert(connection.use_count() == 1);
                
                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();
                if (head.statusCode_ == 200) {
                    rapidjson::Document json; 
                    json.Parse(body.data(), body.size());
                    auto login = json["login"].GetString();
                    auto expiration = json["expires_in"].GetUint64();
                    Console::Write("validation success. Login:", login, "Expire in:", expiration, '\n');
                }
                else {
                    Console::Write("[ERROR] validation failed. Status:"
                        , head.statusCode_
                        , head.reasonPhrase_
                        , '\n', body, '\n'
                    );
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Twitch::Invoker::Execute(command::Shutdown) {
    assert(false && "TODO: not implemented");    
}

void Twitch::Invoker::Execute(command::Join cmd) {
    auto join = twitch::Join{cmd.channel_}.Build();
    assert(true && "TODO: Confirm that connection is alive"
        "after introducing connection state"
    );
    twitch_->irc_->Write(std::move(join), []() {
        Console::Write("send join channel request\n");
    });
}

void Twitch::Invoker::Execute(command::Chat cmd) {
    auto chat = twitch::Chat{cmd.channel_, cmd.message_}.Build();
    assert(true && "TODO: Confirm that connection is alive"
        "after introducing connection state"
    );
    twitch_->irc_->Write(std::move(chat), []() {
        Console::Write("send message tp channel\n");
    });
}

void Twitch::Invoker::Execute(command::Leave cmd) {
    auto leave = twitch::Leave{cmd.channel_}.Build();
    assert(true && "TODO: Confirm that connection is alive"
        "after introducing connection state"
    );
    twitch_->irc_->Write(std::move(leave), []() {
        Console::Write("send part channel request\n");
    });
}

void Twitch::Invoker::Execute(command::Login cmd) {
    auto connectRequest = twitch::IrcAuth{cmd.token_, cmd.user_}.Build();
    assert(twitch_ && "Cannot be null");
    
    // Check to be able reconnect using external approach (not in connection interface)
    // TODO: implement internal one for the connection
    if (twitch_->irc_) {
        twitch_->irc_->Close();
        twitch_->irc_.reset();
    }

    const size_t id { 0 };
    twitch_->irc_ = std::make_shared<IrcConnection>(twitch_->context_
        , twitch_->ssl_
        , twitch::kHost
        , twitch::kService
        , id
    );

    auto onConnect = [request = std::move(connectRequest)
        , twitchService = twitch_
        , irc = twitch_->irc_.get()
    ]() {
        irc->Write(std::move(request), [twitchService, irc]() {
            irc->Read([twitchService, irc]() {
                twitchService->HandleResponse(irc->AcquireResponse());
            });
        });
    };
    twitch_->irc_->Connect(std::move(onConnect));
    
}



} // namespace service