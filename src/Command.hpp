#pragma once
#include <type_traits>
#include <vector>
#include <string_view>
#include <sstream>
#include <string>
#include <cassert>

namespace service {
    class Blizzard;
    class Twitch;
    class Console;
}

namespace command {

    struct RawCommand {
        std::string command_;
        std::vector<std::string> params_;
    };

    using Params = std::vector<std::string_view>;

    struct RealmID {
        static RealmID Create(const service::Blizzard& ctx, const Params& params) {
            return {};
        }
    };

    // initiated by the twitch's user chat message
    // passed by Twitch service to Blizzard
    // executed by Blizzard service: acquire data from the remote server
    struct RealmStatus {
        std::string channel_;  // used by twitch
        std::string initiator_;  // used by twitch

        static RealmStatus Create(const service::Blizzard& ctx, const Params& params);
        static RealmStatus Create(const service::Twitch& ctx, const Params& params);
    };

    struct Arena {
        std::string channel_;  // used by twitch
        std::string initiator_;  // used by twitch

        static Arena Create(const service::Blizzard& ctx, const Params& params);
        static Arena Create(const service::Twitch& ctx, const Params& params);
    };

    struct AccessToken {
        static AccessToken Create(const service::Blizzard& ctx, const Params& params) {
            return {};
        }
    };

    struct Shutdown {
        static Shutdown Create(const service::Console& ctx, const Params& params) {
            return {};
        }
    };

    struct Help {
        static Help Create(const service::Console& ctx, const Params& params) {
            return {};
        }
        static Help Create(const service::Twitch& ctx, const Params& params) {
            return {};
        }
    };
    
    struct Pong {
        static Pong Create(const service::Twitch& ctx, const Params& params) {
            return {};
        }
    };

    struct Chat {
        std::string channel_;
        std::string message_;

        static Chat Create(const service::Twitch& ctx, const Params& params);
    };

    struct Join {
        // https://datatracker.ietf.org/doc/html/rfc1459.html#section-1.3
        std::string channel_;

        static Join Create(const service::Twitch& ctx, const Params& params);
    };

    struct Leave {
        std::string channel_;

        static Leave Create(const service::Twitch& ctx, const Params& params);
    };

    struct Validate {
        static Validate Create(const service::Twitch& ctx, const Params& params) {
            return {};
        }
    };

    struct Login {
        std::string user_;
        std::string token_;

        static Login Create(const service::Twitch& ctx, const Params& params);
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