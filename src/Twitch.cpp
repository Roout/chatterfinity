#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"
#include "Command.hpp"
#include "Utility.hpp"

#include "rapidjson/document.h"

#include <algorithm>

namespace service {

Twitch::Twitch(const Config *config, Container * outbox) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , translator_ {}
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
    , outbox_ { outbox }
{
    assert(config_ && "Config is NULL");
    assert(outbox_ && "Queue is NULL");
    /**
     * [Amazon CA](https://www.amazontrust.com/repository/)
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
        { "help"sv,         Translator::CreateHandle<command::Help>(*this) },
        { "ping"sv,         Translator::CreateHandle<command::Pong>(*this) },
        { "realm-status"sv, Translator::CreateHandle<command::RealmStatus>(*this) }
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
        irc_->ScheduleShutdown();
        irc_.reset();
    }
    // don't stop context to let it finish all jobs 
    // and shutdown connections gracefully
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

namespace {
    std::string ExtractBetween(const std::string& src, char left, char right) {
        const auto leftDelim = src.find(left);
        const auto rightDelim = src.find(right, leftDelim);
        if (leftDelim == std::string::npos || rightDelim == std::string::npos) {
            return {};
        } 
        else {
            return { src.substr(leftDelim + 1, rightDelim - leftDelim - 1) };
        }
    }

    inline std::string_view ShiftView(const std::string& src, size_t shift) noexcept {
        assert(shift < src.size());
        return { src.data() + shift, src.size() - shift};
    }
}

void Twitch::HandleResponse(net::irc::Message message) {
    using IrcCommands = net::irc::IrcCommands;

    constexpr IrcCommands ircCmds;
    enum { kChannel, kCommand, kRequiredFields };

    { // Debug:
        std::string raw = "prefix: " + message.prefix_ 
            + "; command: " + message.command_ + "; params:";
        for (auto& p: message.params_) raw += " " + p;
        Console::Write("[twitch] read:", raw, '\n');
    }

    const auto ircCmdKind = ircCmds.Get(message.command_);
    if (!ircCmdKind) return;

    switch (*ircCmdKind) {
        case IrcCommands::kPrivMsg: {
            // user command, not IRC command
            enum Sign : char { kChannelSign = '#', kCommandSign = '!'};
            auto& ircParams { message.params_ };

            // is user-defined command
            if (ircParams.size() >= kRequiredFields
                && ircParams[kChannel].front() == kChannelSign
                && ircParams[kCommand].front() == kCommandSign
            ) {
                std::transform(ircParams[kCommand].cbegin()
                    , ircParams[kCommand].cend()
                    , ircParams[kCommand].begin()
                    , std::tolower);

                // skipped `kCommandSign`
                auto twitchCommand { ShiftView(ircParams[kCommand], 1) };
                if (auto handle = translator_.GetHandle(twitchCommand); handle) {
                    // username (nick) has to be between (! ... @)
                    auto user = ExtractBetween(message.prefix_, '!', '@');
                    assert(!user.empty() && "wrong understanding of IRC format");
                    // skipped channel prefix: #
                    auto twitchChannel { ShiftView(ircParams[kChannel], 1) };

                    Translator::Params commandParams { twitchChannel, user };
                    Console::Write("[twitch] command:", twitchCommand, 
                        "; params:", commandParams[0], commandParams[1], '\n');
                    std::invoke(*handle, commandParams);
                }
            }
        } break;
        case IrcCommands::kPing: {
            if (auto handle = translator_.GetHandle("ping"); handle) {
                std::invoke(*handle, Translator::Params{});
            }
        } break;
        default: assert(false);
    }
}

void Twitch::Invoker::Execute(command::Help) {
    assert(false && "TODO: not implemented");
}

void Twitch::Invoker::Execute(command::Pong) {
    assert(twitch_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!twitch_->irc_) {
        Console::Write("[twitch] pong: irc connection is not established\n");
    }
    else {
        auto pongRequest = twitch::Pong{}.Build();
        twitch_->irc_->ScheduleWrite(std::move(pongRequest), []() {
            Console::Write("[twitch] send pong request\n");
        });
    }
}

void Twitch::Invoker::Execute(command::Validate) {
    constexpr std::string_view kHost { "id.twitch.tv" };
    constexpr std::string_view kService { "https" };
    const size_t kId { 0 };

    assert(twitch_ && "Cannot be null");

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
        shared->ScheduleWrite(std::move(request), [twitchService, connection]() {
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
                    Console::Write("[twitch] validation success. Login:", login, "Expire in:", expiration, '\n');
                }
                else {
                    Console::Write("[ERROR] [twitch] validation failed. Status:"
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
    assert(twitch_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!twitch_->irc_) {
        Console::Write("[twitch] join: irc connection is not established\n");
    }
    else {
        auto join = twitch::Join{cmd.channel_}.Build();
        twitch_->irc_->ScheduleWrite(std::move(join), []() {
            Console::Write("[twitch] send join channel request\n");
        });
    }
}

void Twitch::Invoker::Execute(command::Chat cmd) {
    assert(twitch_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!twitch_->irc_) {
        Console::Write("[twitch] chat: irc connection is not established\n");
    }
    else {
        auto chat = twitch::Chat{cmd.channel_, cmd.message_}.Build();
        twitch_->irc_->ScheduleWrite(std::move(chat), []() {
            Console::Write("[twitch] send message to channel\n");
        });
    }
}

void Twitch::Invoker::Execute(command::Leave cmd) {
    assert(twitch_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!twitch_->irc_) {
        Console::Write("[twitch] leave: irc connection is not established\n");
    }
    else {
        auto leave = twitch::Leave{cmd.channel_}.Build();
        twitch_->irc_->ScheduleWrite(std::move(leave), []() {
            Console::Write("[twitch] send part channel request\n");
        });
    }
}

void Twitch::Invoker::Execute(command::Login cmd) {
    assert(twitch_ && "Cannot be null");
    // TODO: still need to handle the case when all attempt to reconnect failed!
    if (twitch_->irc_) {
        Console::Write("[twitch] irc connection is already established\n");
        return;
    }

    const size_t id { 0 };
    twitch_->irc_ = std::make_shared<IrcConnection>(twitch_->context_
        , twitch_->ssl_
        , twitch::kHost
        , twitch::kService
        , id
    );

    auto onConnect = [request =twitch::IrcAuth{cmd.token_, cmd.user_}.Build()
        , twitchService = twitch_
        , irc = twitch_->irc_.get()
    ]() {
        irc->ScheduleWrite(std::move(request), [twitchService, irc]() {
            irc->Read([twitchService, irc]() {
                twitchService->HandleResponse(irc->AcquireResponse());
            });
        });
    };
    twitch_->irc_->Connect(std::move(onConnect));
}

void Twitch::Invoker::Execute(command::RealmStatus cmd) {
    assert(twitch_ && "Cannot be null");
    assert(twitch_->irc_ && "Cannot be null");

    Console::Write("[twitch] execute realm-status command:", cmd.channel_, cmd.initiator_, '\n');
    command::RawCommand raw { "realm-status", { std::move(cmd.channel_), std::move(cmd.initiator_) }};
    if (twitch_->outbox_->TryPush(std::move(raw))) {
        Console::Write("[twitch] push `RealmStatus` to queue\n");
    }
    else {
        Console::Write("[twitch] failed to push `RealmStatus` to queue is full\n");
    }
}

} // namespace service