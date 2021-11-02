#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <mutex>

#include "Command.hpp"
#include "Translator.hpp"
#include "Alias.hpp"
#include "Response.hpp" // net::irc::Message

class IrcConnection;

namespace service::twitch {

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using Seconds = std::chrono::seconds;

/**
 * Usage:
 * 1. Use bucket to acquire ticket for irc connection for 
 * command execution (if bucket is not empty)
 * 2. Ticket must be passed to `onWrite` callback in execution chain 
 * and explicitly called Ticket::Release there.
 * 
 * Note: only MessageBucket::refillAmount_ tickets can exist at once.
 * It makes impossible to use `refillAmount_` tickets right before refilling
 * and `refillAmount_` right after(i.e., legaly use 2X tickets in short 
 * amount of time <= `refillRate_`) in short timelapse, 
 * however it prevents usage more than `refillAmount_` tickets at once.
 *
 * As answering to EVERY request of the users deemed not very important
 * I don't enqueue requests which can't get ticket due to rate-limit
 * I just abandon them.
 *
 * TODO: maybe queue message which will notify that some message were ignored
 * and advice to repeat it again
 */
class MessageBucket final {
public:
    class Ticket final {
    public:

        Ticket(MessageBucket* bucket);
        
        void Release() noexcept;

    private:
        // Assume, bucket can't be destroyed 
        // while IrcShard(and IrcConnection) exists
        MessageBucket* const bucket_ { nullptr };
        bool released_ { false };
    };

    MessageBucket(std::uint16_t amount, Seconds rate);

    // Q: Why Not Optional?
    // A: Used shared_ptr instead of optional with move semantic 
    // because capturing move-only object will make move-only lambda 
    // so I won't be able to create callback of type `std::function<void()>`
    // which has requirement Copy[Assignable|Constructable]
    std::shared_ptr<Ticket> TryAcquire();

    void Release();

private:
    
    void TryRefill();

    // `bucket size`
    // differ for user mode and moderator mode
    const std::uint16_t refillAmount_;
    // amount of available messages for IRC connection
    std::uint16_t available_;
    // already consumed during the last refill
    std::uint16_t consumed_;
    // shows how much time needs to pass to fill the bucket
    // Note: differ for user mode and moderator mode
    const Seconds refillRate_;
    TimePoint lastRefillTime_;

    mutable std::mutex mutex_;
};

/**
 * Thread-safety concerns:
 *  IrcShard is being touched (after being constructed) 
 *  only by `Twitch::ResetWork` and `Twitch::Execute`
 *  so these are narrow places.
 * 
 * 1. service::Twitch's destructor returns after all work which is being posted to `io_context`
 *  is done.
 * 2. After completion of all work in `io_context` the `IrcConnection` will exist only in 
 * `IrcShard` (ONLY 1 strong reference). See `assert` in `~IrcShard()`
 */
class IrcShard final {
public:
    using SharedIO = std::shared_ptr<boost::asio::io_context>;
    using SharedSSL = std::shared_ptr<boost::asio::ssl::context>;

    IrcShard(Twitch *service
        , command::Queue *commands
        , command::AliasTable *alias
        , SharedIO context
        , SharedSSL ssl);

    // ASSUME: `irc_->ScheduleShutdown()` has already been called, 
    // i.e., IrcShard::Reset() had been already invoked before the ~IrcShard
    // 
    // TODO: Reset may no be called if excption is thrown. 
    //       Find and resolve such dangerous places.
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
    // Twitch's IRC limits: https://dev.twitch.tv/docs/irc/guide
    static constexpr std::uint16_t kRefillAmount{ 20 };
    static constexpr Seconds kGeneralRefillRate{ 10 };
    static constexpr Seconds kChannelRefillRate{ 30 };
    
    struct Channel {
        std::string name; // unique
    };

    enum Bucket: uint16_t { 
        kGeneral, // PASS, JOIN
        kChannel, // PRIVMSG
        kCount
    };
    std::array<MessageBucket, Bucket::kCount> buckets_;
    
    // Stuff from the twitch context;
    // required for creating HTTP connection
    Twitch *const service_ { nullptr };
    command::Queue *const commands_ { nullptr };
    Translator translator_;
    // Keep bindings of [aliases] to [commands with parameters]
    command::AliasTable *const aliases_ { nullptr };

    // Contexts for connection's creation
    SharedIO context_;
    SharedSSL ssl_;

    // Thread-safety:
    // - `shared_ptr` - used only with const methods so no data race here.
    //   As long as `irc_` is not reseted manually no data race will occur.
    // - `IrcConnection` object - it cannot live more than `IrcShard` because 
    //  the only thing that can prolong it's life is `context_` 
    //  whereas `context_` should complete all work by the time the IrcShard 
    //  will be destroyed.
    std::shared_ptr<IrcConnection> irc_;

    // List of connected channels
    std::vector<Channel> channels_;

    class Invoker;
    std::unique_ptr<Invoker> invoker_;
};

class IrcShard::Invoker {
public:
    Invoker(IrcShard *shard);

    void Execute(command::Help);
    void Execute(command::Ping);
    void Execute(command::Pong);
    void Execute(command::Shutdown);
    void Execute(command::Validate);
    void Execute(command::Login);
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