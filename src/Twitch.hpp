#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <memory>
#include <functional>
#include <vector>
#include <thread>

#include "Token.hpp"
#include "Command.hpp"
#include "Config.hpp"

namespace service {

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class Twitch : public std::enable_shared_from_this<Twitch> {
public:
    Twitch(const Config *config);
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

    void ResetWork();

private:

    void AcquireToken(std::function<void()> continuation = {});

private:
    class Invoker;

    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    AccessToken token_;
    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> ssl_;
    
    const Config * const config_ { nullptr };
    std::unique_ptr<Invoker> invoker_;
};

class Twitch::Invoker {
public:
    Twitch::Invoker(Twitch *twitch) : twitch_ { twitch } {}

    void Execute(command::AccessToken);
    void Execute(command::Help);
    void Execute(command::Shutdown);

private:
    Twitch * const twitch_ { nullptr };
};

template<typename Command, typename Enable>
inline void Twitch::Execute(Command&& cmd) {
    invoker_->Execute(std::forward<Command>(cmd));
}

}