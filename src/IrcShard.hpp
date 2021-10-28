#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>

#include "Command.hpp"
#include "Translator.hpp"
#include "Alias.hpp"
#include "Response.hpp" // net::irc::Message

class IrcConnection;

namespace service::twitch {

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using Seconds = std::chrono::seconds;

class MessageBucket final {
public:
    MessageBucket(std::uint16_t amount, Seconds rate);

    // Actually, it's not precision approach 
    // cuz we lose some tickets(opportunity to write)
    // but I can avoid using timer
    bool TryUse();

private:
    // `bucket size`
    // differ for user mode and moderator mode
    const std::uint16_t refillAmount_;
    // amount of available messages for IRC connection
    std::uint16_t available_;
    // shows how much time needs to pass to fill the bucket
    // Note: differ for user mode and moderator mode
    const Seconds refillRate_;
    TimePoint lastRefillTime_;
};

// namespace ssl = boost::asio::ssl;
// using boost::asio::ip::tcp;

class IrcShard final {
public:
    using SharedIO = std::shared_ptr<boost::asio::io_context>;
    using SharedSSL = std::shared_ptr<boost::asio::ssl::context>;

    IrcShard(Twitch *service
        , command::Queue *commands
        , command::AliasTable *alias
        , SharedIO context
        , SharedSSL ssl);

    ~IrcShard();

    void Reset();

    template<typename Command,
        typename Enable = std::enable_if_t<command::details::is_twitch_api_v<Command>>
    >
    void Execute(Command&& cmd);

private:

    void HandleResponse(net::irc::Message message);

    void HandlePrivateMessage(net::irc::Message& message);

private:
    // IRC limits: https://dev.twitch.tv/docs/irc/guide
    static constexpr std::uint16_t kRefillAmount{ 20 };
    static constexpr Seconds kGeneralRefillRate{ 10 };
    static constexpr Seconds kChannelRefillRate{ 30 };
    
    struct Channel {
        std::string name; // unique
    };

    enum Bucket: uint16_t { 
        kGeneral, // auth + join
        kChannel, // privmsg + cap [sending commands or messages to channels]
        kCount
    };
    const std::array<MessageBucket, Bucket::kCount> buckets_;
    std::unordered_map<std::string_view, Bucket> bucketByCommand_;
    
    // stuff from the twitch context 
    // required for creating HTTP connection
    Twitch *const service_ { nullptr };
    command::Queue *const commands_ { nullptr };
    Translator translator_;
    // keep bindings of [aliases] to [commands with parameters]
    command::AliasTable *const aliases_ { nullptr };

    // connection
    SharedIO context_;
    SharedSSL ssl_;
    std::shared_ptr<IrcConnection> irc_;

    // list of connected channels
    std::vector<Channel> channels_;

    class Invoker;
    std::unique_ptr<Invoker> invoker_;
};

class IrcShard::Invoker {
public:
    Invoker(IrcShard *shard);

    void Execute(command::Help);
    void Execute(command::Shutdown);
    void Execute(command::Validate);
    void Execute(command::Login);
    void Execute(command::Ping);
    void Execute(command::Pong);
    void Execute(command::Join);
    void Execute(command::Leave);
    void Execute(command::Chat);
    void Execute(command::RealmStatus);
    void Execute(command::Arena);

private:
    IrcShard * const shard_ { nullptr };
};


template<typename Command, typename Enable>
inline void IrcShard::Execute(Command&& cmd) {
    invoker_->Execute(std::forward<Command>(cmd));
}

} // namespace service::twitch