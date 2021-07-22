#pragma once
#include <type_traits>
#include <vector>
#include <string_view>
#include <sstream>
#include <string>
#include <cassert>

namespace command {

    struct RawCommand {
        std::string command_;
        std::vector<std::string> params_;
    };

    struct RealmID {
        static RealmID Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct RealmStatus {
        static RealmStatus Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct AccessToken {
        static AccessToken Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct Shutdown {
        static Shutdown Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct Help {
        static Help Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct Validate {
        static Validate Create(const std::vector<std::string_view>&) {
            return {};
        }
    };

    struct Login {
        std::string nick_;
        std::string pass_;

        static Login Create(const std::vector<std::string_view>& params) {
            assert(params.size() == 2);
            return { std::string(params[0]), std::string(params[1]) };
        }
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
                std::is_same_v<T, Shutdown>
                || std::is_same_v<T, Help>
                || std::is_same_v<T, Validate>
                || std::is_same_v<T, Login>
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