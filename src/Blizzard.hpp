#pragma once

#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Command.hpp"
#include "Cache.hpp"
#include "ConcurrentQueue.hpp"
#include "Environment.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class Config;

namespace service {

class Blizzard 
    : public std::enable_shared_from_this<Blizzard> 
{
public:
    using Container = CcQueue<command::RawCommand, cst::kQueueCapacity>;
    
    Blizzard(const Config *config, Container * outbox);

    Blizzard(const Blizzard&) = delete;
    Blizzard(Blizzard&&) = delete;
    Blizzard& operator=(const Blizzard&) = delete;
    Blizzard& operator=(Blizzard&&) = delete;

    ~Blizzard();

    template<typename Command,
        typename = std::enable_if_t<command::details::is_blizzard_api_v<Command>>
    >
    void Execute(Command&& cmd);

    void ResetWork();

    void Run();

    const Config* GetConfig() const noexcept {
        return config_;
    }

private:
    using Callback = std::function<void()>;

    void QueryRealm(Callback continuation);

    void QueryRealmStatus(command::RealmStatus cmd, Callback continuation);

    void AcquireToken(Callback continuation);

    size_t GenerateId() const;

private:
    class Invoker;

    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    // TODO: Add everything to cache-table
    CacheSlot token_;
    CacheSlot arena_;
    CacheSlot realm_;

    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> ssl_;

    std::unique_ptr<Invoker> invoker_;
    const Config * const config_ { nullptr };
    Container * const outbox_ { nullptr };
    
    // connection id
    static inline size_t lastID_ { 0 };
};

class Blizzard::Invoker {
public:
    Invoker(Blizzard *blizzard) : blizzard_ { blizzard } {}

    void Execute(command::RealmID);
    void Execute(command::RealmStatus);
    void Execute(command::AccessToken);
    void Execute(command::Arena);

private:
    Blizzard * const blizzard_ { nullptr };
};

template<typename Command, typename Enable>
inline void Blizzard::Execute(Command&& cmd) {
    invoker_->Execute(std::forward<Command>(cmd));
}

inline size_t Blizzard::GenerateId() const {
    return Blizzard::lastID_++;
}

} // namespace service