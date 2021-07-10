#pragma once

#include <memory>
#include <cassert>
#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Command.hpp"
#include "Console.hpp" // TODO: remove this header
#include "Connection.hpp"
#include "Request.hpp"
#include "Token.hpp"
#include "Config.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;
    
// service
class Blizzard : public std::enable_shared_from_this<Blizzard> {
public:
    Blizzard(std::shared_ptr<ssl::context> ssl);

    Blizzard(const Blizzard&) = delete;
    Blizzard(Blizzard&&) = delete;
    Blizzard& operator=(const Blizzard&) = delete;
    Blizzard& operator=(Blizzard&&) = delete;

    ~Blizzard();

    template<typename Command,
        typename = std::enable_if_t<command::details::is_blizzard_api_v<Command>>
    >
    void Execute([[maybe_unused]] Command& cmd);

    void ResetWork();

    void Run();

    void QueryRealm(std::function<void(size_t realmId)> continuation = {}) const;

    void QueryRealmStatus(size_t realmId, std::function<void()> continuation = {}) const;

    void AcquireToken(std::function<void()> continuation = {});

private:
    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    
    size_t GenerateId() const;

    static constexpr size_t kThreads { 2 };
    std::vector<std::thread> threads_;

    Token token_;
    std::shared_ptr<boost::asio::io_context> context_;
    Work work_;
    std::shared_ptr<ssl::context> sslContext_;

    Config config_;

    // connection id
    static inline size_t lastID_ { 0 };
};

template<typename Command,
    typename Enable // = std::enable_if_t<command::details::is_blizzard_api_v<Command>>
>
inline void Blizzard::Execute([[maybe_unused]] Command& cmd) {
    if constexpr (std::is_same_v<Command, command::RealmID>) {
        auto initiateRealmQuery = [weak = weak_from_this()]() {
            if (auto self = weak.lock(); self) {
                self->QueryRealm([weak](size_t realmId) {
                    if (auto self = weak.lock(); self) {
                        Console::Write("ID acquired:", realmId, '\n');
                    }
                });
            }
        };
        if (!token_.IsValid()) {
            AcquireToken(std::move(initiateRealmQuery));
        }
        else {
            std::invoke(initiateRealmQuery);
        }
    }
    else if (std::is_same_v<Command, command::RealmStatus>) {
        auto initiateRealmQuery = [weak = weak_from_this()]() {
            if (auto self = weak.lock(); self) {
                self->QueryRealm([weak](size_t realmId) {
                    if (auto self = weak.lock(); self) {
                        Console::Write("ID acquired: ", realmId, '\n');
                        self->QueryRealmStatus(realmId, [weak]() {
                            if (auto self = weak.lock(); self) {
                                Console::Write("Realm confirmed!\n");
                            }
                        });
                    }
                });
            }
        };
        if (!token_.IsValid()) {
            AcquireToken(std::move(initiateRealmQuery));
        }
        else {
            std::invoke(initiateRealmQuery);
        }
    }
    else if (std::is_same_v<Command, command::AccessToken>) {
        AcquireToken([weak = weak_from_this()]() {
            if (auto self = weak.lock(); self) {
                Console::Write("Token acquired.\n");
            }
        });
    }
    else {
        assert(false && "Unreachable: Unknown blizzard command");
    }
}

inline size_t Blizzard::GenerateId() const {
    return Blizzard::lastID_++;
}