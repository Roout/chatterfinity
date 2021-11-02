#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <memory>
#include <functional>
#include <vector>
#include <thread>

#include "Config.hpp"
#include "Command.hpp"
#include "IrcShard.hpp"

// forward declaration
namespace command {
    class AliasTable;
}

namespace service {

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class Twitch 
    : public std::enable_shared_from_this<Twitch> 
{
public:
    Twitch(const Config *config
        , command::Queue *outbox
        , command::AliasTable *aliases);
    ~Twitch();
    Twitch(const Twitch&) = delete;
    Twitch(Twitch&&) = delete;
    Twitch& operator=(const Twitch&) = delete;
    Twitch& operator=(Twitch&&) = delete;

    void Run();

    template<typename Command,
        typename Enable = std::enable_if_t<command::details::is_twitch_api_v<Command>>
    >
    void Execute(Command&& cmd);

    // Called only from App::~App
    void ResetWork();

    const Config* GetConfig() const noexcept {
        return config_;
    }

private:
    template<class T>
    using executor_work_guard = boost::asio::executor_work_guard<T>;
    using executor_type = boost::asio::io_context::executor_type;
    using Work = executor_work_guard<executor_type>;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> ssl_;
    
    // keep twitch's secret data
    const Config * const config_ { nullptr };
    // It's been seen as readonly unique_ptr by App's worker threads
    // Note: in call-chains it's only been dereferenced! (readonly access)
    std::unique_ptr<twitch::IrcShard> shard_;
};

template<typename Command, typename Enable>
inline void Twitch::Execute(Command&& cmd) {
    shard_->Execute(std::forward<Command>(cmd));
}

} // namespace service