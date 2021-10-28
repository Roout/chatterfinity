#include "IrcShard.hpp"

#include "Connection.hpp"
#include "Request.hpp"
#include "Chain.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Twitch.hpp"

#include "rapidjson/document.h"

#include <cassert>
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

namespace service::twitch {

MessageBucket::MessageBucket(std::uint16_t amount, Seconds rate)
    : refillAmount_ { amount }
    , available_ { amount }
    , refillRate_ { rate }
    , lastRefillTime_ { std::chrono::steady_clock::now() }
{}

// Actually, it's not precision approach 
// cuz we lose some tickets(opportunity to write)
// but I can avoid using timer
bool MessageBucket::TryUse() {
    TimePoint now = std::chrono::steady_clock::now();
    Seconds elapsed = std::chrono::duration_cast<Seconds>(lastRefillTime_ - now);
    
    if (elapsed >= refillRate_) {
        lastRefillTime_ = now;
        available_ = refillAmount_;
    }

    if (available_ > 0) {
        available_--;
        return true;
    }
    return false;
}

IrcShard::IrcShard(Twitch *service
    , command::Queue *rawCommands
    , command::AliasTable *aliases
    , SharedIO context
    , SharedSSL ssl
) 
    : buckets_ { 
        MessageBucket  { kRefillAmount, kGeneralRefillRate }
        , { kRefillAmount, kChannelRefillRate }
    }
    , service_ { service }
    , commands_ { rawCommands }
    , aliases_ { aliases }
    , translator_ {}
    , context_ { context }
    , ssl_ { ssl }
    , invoker_ { std::make_unique<Invoker>(this) }
{
    assert(service);
    assert(rawCommands);
    assert(context && ssl);
    
    using namespace std::literals::string_view_literals;
    std::initializer_list<Translator::Pair> commands {
        { "help"sv,         Translator::CreateHandle<command::Help>(*service) },
        // TODO: rename to pong!
        { "ping"sv,         Translator::CreateHandle<command::Pong>(*service) },
        { "arena"sv,        Translator::CreateHandle<command::Arena>(*service) },
        { "realm-status"sv, Translator::CreateHandle<command::RealmStatus>(*service) }
    };
    translator_.Insert(commands);

    const size_t id { 0 };
    irc_ = std::make_shared<IrcConnection>(context_
        , ssl_
        , request::twitch::kHost
        , request::twitch::kService
        , id);
}

IrcShard::~IrcShard() {
    Reset();
}

void IrcShard::Reset() {
    if (irc_) {
        irc_->ScheduleShutdown();
        irc_.reset();
    }
}

// ===================== RESPONSE ================== //
void IrcShard::HandlePrivateMessage(net::irc::Message& message) {
    enum { kChannel, kMessage, kRequiredFields };
    static_assert(kMessage == 1, "According to IRC format message "
        "for PRIVMSG command is always second parameter"
        "(numeration starting with 0)");
    
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
        if (auto referred = aliases_->GetCommand(chatCommand); referred) {
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
}

void IrcShard::HandleResponse(net::irc::Message message) {
    using IrcCommands = net::irc::IrcCommands;

    { // Debug:
        std::string raw;
        for (auto&& [key, val]: message.tags_) raw += key + "=" + val + ";";
        raw += " prefix: " + message.prefix_ 
            + "; command: " + message.command_ 
            + "; params (" + std::to_string(message.params_.size()) + "):";
        for (auto& p: message.params_) raw += " " + p;
        Console::Write("[twitch] read:", raw, '\n');
    }

    constexpr IrcCommands ircCmds;

    const auto ircCmdKind = ircCmds.Get(message.command_);
    if (!ircCmdKind) return;

    switch (*ircCmdKind) {
        case IrcCommands::kPrivMsg: {
            HandlePrivateMessage(message);
        } break;
        case IrcCommands::kPing: {
            if (auto handle = translator_.GetHandle("ping"); handle) {
                std::invoke(*handle, Translator::Params{});
            }
        } break;
        default: assert(false);
    }
}
// ===================== INVOKER =================== //

IrcShard::Invoker::Invoker(IrcShard *shard) 
    : shard_ { shard } 
{
    assert(shard);
}


void IrcShard::Invoker::Execute(command::Help) {
    assert(false && "TODO: not implemented");
}

void IrcShard::Invoker::Execute(command::Ping cmd) {
    assert(shard_ && "Cannot be null");
    if (!shard_->irc_) {
        Console::Write("[twitch] ping: irc connection is not established\n");
    }
    else {
        auto pingRequest = request::twitch::Ping{cmd.channel_}.Build();
        shard_->irc_->ScheduleWrite(std::move(pingRequest), []() {
            Console::Write("[twitch] send pong request\n");
        });
    }
}

void IrcShard::Invoker::Execute(command::Pong) {
    assert(shard_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!shard_->irc_) {
        Console::Write("[twitch] pong: irc connection is not established\n");
    }
    else {
        auto pongRequest = request::twitch::Pong{}.Build();
        shard_->irc_->ScheduleWrite(std::move(pongRequest), []() {
            Console::Write("[twitch] send pong request\n");
        });
    }
}

void IrcShard::Invoker::Execute(command::Validate) {
    constexpr std::string_view kHost { "id.twitch.tv" };
    constexpr std::string_view kService { "https" };
    const size_t kId { 0 };

    assert(shard_ && "Cannot be null");
    assert(shard_->service_ && "Cannot be null");

    auto connection = std::make_shared<HttpConnection>(
        shard_->context_, shard_->ssl_, kHost, kService, kId
    );

    const Config::Identity kIdentity { "twitch" };
    const Config *config = shard_->service_->GetConfig();
    const auto secret = config->GetSecret(kIdentity);
    if (!secret) {
        throw std::runtime_error("Fail to create config");
    }

    auto weak = utils::WeakFrom<HttpConnection>(connection);
    auto connect = [connection](Chain::Callback cb) {
        connection->Connect(std::move(cb));
    };

    auto request = request::twitch::Validation{ secret->token_ }.Build();
    auto write = [connection, req = std::move(request)](Chain::Callback cb) {
        connection->ScheduleWrite(std::move(req), std::move(cb));
    };
    
    auto read = [connection](Chain::Callback cb) {
        connection->Read(std::move(cb));
    };
    
    auto readCallback = [weak]() {
        auto shared = weak.lock();
        const auto [head, body] = shared->AcquireResponse();
        if (head.statusCode_ == 200) {
            rapidjson::Document json; 
            json.Parse(body.data(), body.size());
            auto login = json["login"].GetString();
            auto expiration = json["expires_in"].GetUint64();
            Console::Write("[twitch] validation success. Login:", login
                , "Expire in:", expiration, '\n');
        }
        else {
            Console::Write("[ERROR] [twitch] validation failed. Status:"
                , head.statusCode_, head.reasonPhrase_
                , '\n', body, '\n'
            );
        }
    };

    auto chain = std::make_shared<Chain>(shard_->context_);
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback))
        .Execute();
}

void IrcShard::Invoker::Execute(command::Shutdown) {
    assert(false && "TODO: not implemented");    
}

void IrcShard::Invoker::Execute(command::Join cmd) {
    assert(shard_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!shard_->irc_) {
        Console::Write("[twitch] join: irc connection is not established\n");
    }
    else {
        auto join = request::twitch::Join{cmd.channel_}.Build();
        shard_->irc_->ScheduleWrite(std::move(join), []() {
            Console::Write("[twitch] send join channel request\n");
        });
    }
}

void IrcShard::Invoker::Execute(command::Chat cmd) {
    assert(shard_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!shard_->irc_) {
        Console::Write("[twitch] chat: irc connection is not established\n");
    }
    else {
        auto chat = request::twitch::Chat{cmd.channel_, cmd.message_}.Build();
        Console::Write("[twitch] trying to send message:"
            , chat.substr(0, chat.size() - 2), "\n");
        shard_->irc_->ScheduleWrite(std::move(chat), []() {
            Console::Write("[twitch] sent message to channel\n");
        });
    }
}

void IrcShard::Invoker::Execute(command::Leave cmd) {
    assert(shard_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when `irc_` is alive but failed [re-]connect!
    if (!shard_->irc_) {
        Console::Write("[twitch] leave: irc connection is not established\n");
    }
    else {
        auto leave = request::twitch::Leave{cmd.channel_}.Build();
        shard_->irc_->ScheduleWrite(std::move(leave), []() {
            Console::Write("[twitch] send part channel request\n");
        });
    }
}

void IrcShard::Invoker::Execute(command::Login cmd) {
    assert(shard_ && "Cannot be null");
    // TODO: still need to handle the case 
    // when all attempt to reconnect failed!
    if (!shard_->irc_) {
        Console::Write("[twitch] login: irc connection is not established\n");
    }

    auto request = request::twitch::IrcAuth{cmd.token_, cmd.user_}.Build();
    auto irc = shard_->irc_.get();
    auto readCallback = [irc, shard = shard_](){
        shard->HandleResponse(irc->AcquireResponse());
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

    auto chain = std::make_shared<Chain>(shard_->context_);
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback))
        .Execute();
}

void IrcShard::Invoker::Execute(command::RealmStatus cmd) {
    assert(shard_ && "Cannot be null");
    assert(shard_->irc_ && "Cannot be null");

    Console::Write("[twitch] execute realm-status command:"
        , cmd.channel_, cmd.user_, '\n');
    command::RawCommand raw { "realm-status", { 
        command::ParamData { "channel", std::move(cmd.channel_) }
        , { "user", std::move(cmd.user_) } }
    };

    if (shard_->commands_->TryPush(std::move(raw))) {
        Console::Write("[twitch] push `RealmStatus` to queue\n");
    }
    else {
        Console::Write("[twitch] failed to push "
            "`RealmStatus` to queue is full\n");
    }
}

void IrcShard::Invoker::Execute(command::Arena cmd) {
    assert(shard_ && "Cannot be null");
    assert(shard_->irc_ && "Cannot be null");

    Console::Write("[twitch] execute arena command:"
        , cmd.channel_, cmd.user_,  cmd.player_, '\n');

    command::RawCommand raw { "arena", { 
        command::ParamData { "channel", std::move(cmd.channel_) }
        , { "user", std::move(cmd.user_) } 
        , { "player", std::move(cmd.player_) } }
    };

    if (shard_->commands_->TryPush(std::move(raw))) {
        Console::Write("[twitch] push `arena` to queue\n");
    }
    else {
        Console::Write("[twitch] failed to push `arena` to queue is full\n");
    }
}

} // namespace service::twitch