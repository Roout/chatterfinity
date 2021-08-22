#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"
#include "Command.hpp"
#include "Utility.hpp"
#include "Alias.hpp"
#include "Chain.hpp"

#include "rapidjson/document.h"

#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <sstream>

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

namespace service {

Twitch::Twitch(const Config *config
    , Container * outbox
    , command::AliasTable * aliases
) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , translator_ {}
    , config_ { config }
    , outbox_ { outbox }
    , aliases_ { aliases }
    , invoker_ { std::make_unique<Invoker>(this) }
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
    std::initializer_list<Translator::Pair> commands {
        { "help"sv,         Translator::CreateHandle<command::Help>(*this) },
        { "ping"sv,         Translator::CreateHandle<command::Pong>(*this) },
        { "arena"sv,        Translator::CreateHandle<command::Arena>(*this) },
        { "realm-status"sv, Translator::CreateHandle<command::RealmStatus>(*this) }
    };
    translator_.Insert(commands);
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

void Twitch::HandleResponse(net::irc::Message message) {
    using IrcCommands = net::irc::IrcCommands;

    constexpr IrcCommands ircCmds;

    { // Debug:
        std::string raw;
        for (auto&& [key, val]: message.tags_) raw += key + "=" + val + ";";
        raw += " prefix: " + message.prefix_ 
            + "; command: " + message.command_ 
            + "; params (" + std::to_string(message.params_.size()) + "):";
        for (auto& p: message.params_) raw += " " + p;
        Console::Write("[twitch] read:", raw, '\n');
    }

    const auto ircCmdKind = ircCmds.Get(message.command_);
    if (!ircCmdKind) return;

    // TODO: slice bloated switch
    switch (*ircCmdKind) {
        case IrcCommands::kPrivMsg: {
            enum { kChannel, kMessage, kRequiredFields };
            static_assert(kMessage == 1, "According to IRC format message "
                "for PRIVMSG command is always second parameter");

            // TODO: either add alias sign 
            // either use ! as sign for both alias and original command
            
            // user command, not IRC command
            enum Sign : char { kChannelSign = '#', kCommandSign = '!'};
            auto& ircParams { message.params_ };

            // is user-defined command
            if (ircParams.size() == kRequiredFields
                && ircParams[kChannel].front() == kChannelSign
                && ircParams[kMessage].front() == kCommandSign
            ) {
                // chat message is an input from the user in twitch chat
                auto& chatMessage { ircParams[kMessage] };
                std::transform(chatMessage.cbegin()
                    , chatMessage.cend()
                    , chatMessage.begin()
                    , [](unsigned char c) { return std::tolower(c); }
                );

                std::string_view unprocessed { chatMessage };
                size_t chatCommandEnd = unprocessed.find_first_of(' ');
                if (chatCommandEnd == std::string_view::npos) {
                    chatCommandEnd = unprocessed.size();
                }
                // here we're still not sure whether it's a command or just a coincidence
                std::string_view chatCommand { unprocessed.data() + 1, chatCommandEnd - 1 };
                unprocessed.remove_prefix(chatCommandEnd + 1);
                // divide message to tokens (maybe parameters for the chat command)
                auto params = command::ExtractArgs(unprocessed, ' ');

                assert(aliases_);
                // Substitute alias with command and required params if it's alias
                auto referred = aliases_->GetCommand(chatCommand);
                if (referred) { 
                    Console::Write("[twitch] used alias "
                        , chatCommand, "refers to "
                        , referred->command, '\n');
                    chatCommand = referred->command;
                    for (const auto& [k, v]: referred->params) {
                        const auto& key = k;
                        if (auto it = std::find_if(params.cbegin(), params.cend(), 
                            [&key](const command::ParamView& data) {
                                return data.key_ == key; 
                            }); it == params.cend()
                        ) { // add param to param list if it's not already provided by user
                            params.push_back(command::ParamView{ 
                                std::string_view{ k }, std::string_view{ v } });
                        }
                    }
                }       

                Console::Write("[twitch-debug] process possible command:", chatCommand, '\n');
                // skipped `kCommandSign`
                if (auto handle = translator_.GetHandle(chatCommand); handle) {
                    // username (nick) has to be between (! ... @)
                    auto user = ::ExtractBetween(message.prefix_, '!', '@');
                    assert(!user.empty() && "wrong understanding of IRC format");
                    // skipped channel prefix: #
                    auto twitchChannel { ::ShiftView(ircParams[kChannel], 1) };

                    command::Args commandParams { 
                        command::ParamView { "channel", twitchChannel },
                        command::ParamView { "user", user } };

                    for (auto&& param: params) {
                        commandParams.push_back(param);
                    }

                    std::stringstream ss;
                    for (auto&& [k, v]: params) ss << k << " " << v << ' '; 
                    Console::Write("[twitch] params:", ss.str(), '\n');
                    
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
        throw std::runtime_error("Fail to create config");
    }

    auto weak = utils::WeakFrom<HttpConnection>(connection);
    auto connect = [weak](Chain::Callback cb) {
        assert(weak.use_count() == 1);
        auto shared = weak.lock();
        shared->Connect(std::move(cb));
    };

    auto request = twitch::Validation{ secret->token_ }.Build();
    auto write = [weak, request = std::move(request)](Chain::Callback cb) {
        assert(weak.use_count() == 1);
        auto shared = weak.lock();
        shared->ScheduleWrite(std::move(request), std::move(cb));
    };
    
    auto read = [weak](Chain::Callback cb) {
        assert(weak.use_count() == 1);
        auto shared = weak.lock();
        shared->Read(std::move(cb));
    };
    
    auto readCallback = [weak](){
        auto shared = weak.lock();
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

    auto chain = std::make_shared<Chain>();
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback))
        .Execute();
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
        Console::Write("[twitch] trying to send message:", chat.substr(0, chat.size() - 2), "\n");
        twitch_->irc_->ScheduleWrite(std::move(chat), []() {
            Console::Write("[twitch] sent message to channel\n");
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

    auto request = twitch::IrcAuth{cmd.token_, cmd.user_}.Build();
    auto irc = twitch_->irc_.get();
    auto readCallback = [irc, twitch = twitch_](){
        twitch->HandleResponse(irc->AcquireResponse());
    };

    auto connect = [irc](Chain::Callback cb) {
        irc->Connect(std::move(cb));
    };
    auto write = [irc, request = std::move(request)](Chain::Callback cb) {
        irc->ScheduleWrite(std::move(request), std::move(cb));
    };
    auto read = [irc](Chain::Callback cb){
        irc->Read(std::move(cb));
    };

    auto chain = std::make_shared<Chain>();
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback))
        .Execute();
}

void Twitch::Invoker::Execute(command::RealmStatus cmd) {
    assert(twitch_ && "Cannot be null");
    assert(twitch_->irc_ && "Cannot be null");

    Console::Write("[twitch] execute realm-status command:", cmd.channel_, cmd.user_, '\n');
    command::RawCommand raw { "realm-status", { 
        command::ParamData { "channel", std::move(cmd.channel_) }
        , { "user", std::move(cmd.user_) } }
    };
    if (twitch_->outbox_->TryPush(std::move(raw))) {
        Console::Write("[twitch] push `RealmStatus` to queue\n");
    }
    else {
        Console::Write("[twitch] failed to push `RealmStatus` to queue is full\n");
    }
}

void Twitch::Invoker::Execute(command::Arena cmd) {
    assert(twitch_ && "Cannot be null");
    assert(twitch_->irc_ && "Cannot be null");

    Console::Write("[twitch] execute arena command:"
        , cmd.channel_, cmd.user_,  cmd.player_, '\n');

    command::RawCommand raw { "arena", { 
        command::ParamData { "channel", std::move(cmd.channel_) }
        , { "user", std::move(cmd.user_) } 
        , { "player", std::move(cmd.player_) } }
    };

    if (twitch_->outbox_->TryPush(std::move(raw))) {
        Console::Write("[twitch] push `arena` to queue\n");
    }
    else {
        Console::Write("[twitch] failed to push `arena` to queue is full\n");
    }
}

} // namespace service