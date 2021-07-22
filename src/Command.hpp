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

    struct RealmStatus {
        static RealmStatus Create(const service::Blizzard& ctx, const Params& params) {
            return {};
        }
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
                || std::is_same_v<T, Pong>
            };
        };

        template<typename T>
        constexpr bool is_blizzard_api_v = is_blizzard_api<T>::value;
        template<typename T>
        constexpr bool is_console_api_v = is_console_api<T>::value;
        template<typename T>
        constexpr bool is_twitch_api_v = is_twitch_api<T>::value;

        template<typename T>
        constexpr bool is_service_v { is_blizzard_api_v<T>
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