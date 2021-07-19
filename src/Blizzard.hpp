#pragma once

#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Command.hpp"
#include "Token.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class Config;

namespace service {
/**
 * TODO:
 * - [ ] save token:
 *  Token can last quite a lot so I don't need to request new one each time
*/
class Blizzard : public std::enable_shared_from_this<Blizzard> {
public:
    Blizzard(const Config *config);

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

private:

    void QueryRealm(std::function<void(size_t realmId)> continuation = {});

    void QueryRealmStatus(size_t realmId, std::function<void()> continuation = {});

    void AcquireToken(std::function<void()> continuation = {});

    size_t GenerateId() const;

private:
    class Invoker;

    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    AccessToken token_;
    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> ssl_;

    std::unique_ptr<Invoker> invoker_;
    const Config * const config_ { nullptr };
    // connection id
    static inline size_t lastID_ { 0 };
};

class Blizzard::Invoker {
public:
    Blizzard::Invoker(Blizzard *blizzard) : blizzard_ { blizzard } {}

    void Execute(command::RealmID);
    void Execute(command::RealmStatus);
    void Execute(command::AccessToken);

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