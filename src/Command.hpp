#pragma once
#include <type_traits>
#include <vector>
#include <string_view>
#include <sstream>
#include <string>
#include <cassert>

#include "Environment.hpp"
#include "ConcurrentQueue.hpp"

namespace service {
    class Blizzard;
    class Twitch;
    class Console;
}

namespace command {

    template<class T>
    struct Param { 
        T key_;
        T value_;
    };
    
    // points to resource
    using ParamView = Param<std::string_view>;
    // owns resource
    using ParamData = Param<std::string>;

    using Args = std::vector<ParamView>;

    // ExtractArgs line `src` for key-value pairs 
    // 
    // <code>
    // std::string equal_delim { "!arena -season=\"1\" -empty= -players=2 -nick=\"шаркии\" -team=\"Эксодус\" -rank=6 -chuck=шаркии" };
    // ExtractArgs(equal_delim,'=');
    // std::string space_delim { "!chat -empty -channel    mainfinity   -message   \"Hello fuck\"" };
    // ExtractArgs(space_delim,' ');
    // </code>
    // 
    // value is not allowed to start with '-';
    // if you need '-' use quotes
    command::Args ExtractArgs(std::string_view src, char key_delimiter);

    struct RawCommand {
        std::string command_;
        std::vector<ParamData> params_;
    };

    using Queue = CcQueue<command::RawCommand, cst::kQueueCapacity>;

    struct Alias {
        static constexpr std::string_view kIdentity = "alias";

        std::string alias_;
        std::string command_;
        std::vector<ParamData> params_;

        static Alias Create(const service::Console&, const Args&);
    };

    struct RealmID {
        static constexpr std::string_view kIdentity = "realm-id";

        static RealmID Create(const service::Blizzard&, const Args&) {
            return {};
        }
    };

    // initiated by the twitch's user chat message
    // passed by Twitch service to Blizzard
    // executed by Blizzard service: acquire data from the remote server
    struct RealmStatus {
        static constexpr std::string_view kIdentity = "realm-status";

        std::string channel_;
        // initiator of the command
        std::string user_;

        static RealmStatus Create(const service::Blizzard& ctx, const Args& params);
        static RealmStatus Create(const service::Twitch& ctx, const Args& params);
    };

    struct Arena {
        static constexpr std::string_view kIdentity = "arena";

        std::string channel_;
        std::string user_;
        std::string player_;

        static Arena Create(const service::Blizzard& ctx, const Args& params);
        static Arena Create(const service::Twitch& ctx, const Args& params);
    };

    struct AccessToken {
        static constexpr std::string_view kIdentity = "token";

        static AccessToken Create(const service::Blizzard&, const Args&) {
            return {};
        }
    };

    struct Shutdown {
        static constexpr std::string_view kIdentity = "shutdown";

        static Shutdown Create(const service::Console&, const Args&) {
            return {};
        }
    };

    struct Help {
        static constexpr std::string_view kIdentity = "help";

        static Help Create(const service::Console&, const Args&) {
            return {};
        }
        static Help Create(const service::Twitch&, const Args&) {
            return {};
        }
    };
    
    struct Pong {
        static constexpr std::string_view kIdentity = "pong";

        static Pong Create(const service::Twitch&, const Args&) {
            return {};
        }
    };

    struct Ping {
        static constexpr std::string_view kIdentity = "ping";

        std::string channel_;
        
        static Ping Create(const service::Twitch&, const Args&);
    };

    struct Chat {
        static constexpr std::string_view kIdentity = "chat";

        std::string channel_;
        std::string message_;

        static Chat Create(const service::Twitch& ctx, const Args& params);
    };

    struct Join {
        static constexpr std::string_view kIdentity = "join";

        // https://datatracker.ietf.org/doc/html/rfc1459.html#section-1.3
        std::string channel_;

        static Join Create(const service::Twitch& ctx, const Args& params);
    };

    struct Leave {
        static constexpr std::string_view kIdentity = "leave";

        std::string channel_;

        static Leave Create(const service::Twitch& ctx, const Args& params);
    };

    struct Validate {
        static constexpr std::string_view kIdentity = "validate";

        static Validate Create(const service::Twitch&, const Args&) {
            return {};
        }
    };

    struct Login {
        static constexpr std::string_view kIdentity = "login";

        std::string user_;
        std::string token_;

        static Login Create(const service::Twitch& ctx, const Args& params);
    };

    namespace details {
    // Type traits:
        template<typename T>
        struct is_blizzard_api {
            static constexpr bool value { 
                std::is_same_v<T, RealmID>
                || std::is_same_v<T, RealmStatus>
                || std::is_same_v<T, Arena>
                || std::is_same_v<T, AccessToken>
            };
        };

        template<typename T>
        struct is_console_api {
            static constexpr bool value { 
                std::is_same_v<T, Shutdown>
                || std::is_same_v<T, Help>
                || std::is_same_v<T, Alias>
            };
        };

        template<typename T>
        struct is_twitch_api {
            static constexpr bool value { 
                std::is_same_v<T, Help>
                || std::is_same_v<T, Validate>
                || std::is_same_v<T, Login>
                || std::is_same_v<T, Join>
                || std::is_same_v<T, Leave>
                || std::is_same_v<T, Pong>
                || std::is_same_v<T, Ping>
                || std::is_same_v<T, RealmStatus> // pass it to the next layer (App)
                || std::is_same_v<T, Arena> // pass it to the next layer (App)
                || std::is_same_v<T, Chat>
            };
        };

        template<typename T>
        constexpr bool is_blizzard_api_v = is_blizzard_api<T>::value;
        template<typename T>
        constexpr bool is_console_api_v = is_console_api<T>::value;
        template<typename T>
        constexpr bool is_twitch_api_v = is_twitch_api<T>::value;

        template<typename T>
        constexpr bool is_service_v { 
            is_blizzard_api_v<T>
            || is_console_api_v<T>
            || is_twitch_api_v<T>
        };
    }

// Helper functions:
    template<typename Command, typename Executor>
    void Execute(Command&& cmd, Executor& ex) {
        static_assert(details::is_service_v<Command>, "Trying to execute unknown command");
        ex.Execute(std::forward<Command>(cmd));
    }

}