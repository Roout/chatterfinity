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
#include "Connection.hpp"

namespace service {

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;


/**
 * NOTE:
 *  - Uses Client credentials flow
 *  https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#oauth-client-credentials-flow
 *  - 
 * 
 * TODO: 
 * - [ ] validation: can add field "last_validation" to Token and check it before queries
 *  You must validate access tokens before making API requests 
 *  which perform mutations on or access sensitive information of users, 
 *  if it has been more than one hour since the last validation. 
 * - [ ] save token:
 *  Token can last quite a lot so I don't need to request new one each time
 */
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

    void Login(std::function<void()> continuation = {});

private:
    class Invoker;

    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    AccessToken token_;
    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> ssl_;
    // std::unique_ptr<IrcConnection> irc_;
    
    const Config * const config_ { nullptr };
    std::unique_ptr<Invoker> invoker_;
};

class Twitch::Invoker {
public:
    Twitch::Invoker(Twitch *twitch) : twitch_ { twitch } {}

    void Execute(command::AccessToken);
    void Execute(command::Help);
    void Execute(command::Shutdown);
    void Execute(command::Login);

private:
    Twitch * const twitch_ { nullptr };
};

template<typename Command, typename Enable>
inline void Twitch::Execute(Command&& cmd) {
    invoker_->Execute(std::forward<Command>(cmd));
}

}