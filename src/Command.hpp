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
        static RealmID Create([[maybe_unused]] const std::vector<std::string_view>& params) {
            return RealmID{};
        }
    };

    struct RealmStatus {
        static RealmStatus Create([[maybe_unused]] const std::vector<std::string_view>& params) {
            return RealmStatus{};
        }
    };

    struct AccessToken {
        static AccessToken Create([[maybe_unused]] const std::vector<std::string_view>& params) {
            return AccessToken{};
        }
    };

    struct Shutdown {
        static Shutdown Create(const std::vector<std::string_view>& params) {
            // TODO: parse!
            return { };
        }
    };

    struct Help {
        static Help Create(const std::vector<std::string_view>& params) {
            // TODO: parse!
            return { };
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
        constexpr bool is_blizzard_api_v = is_blizzard_api<T>::value;
        template<typename T>
        constexpr bool is_console_api_v = is_console_api<T>::value;
    }

// Helper functions:
    template<typename Command, typename Executor>
    void Execute(Command&& cmd, Executor& ex) {
        static_assert(details::is_blizzard_api_v<Command> 
            || details::is_console_api_v<Command>, "Trying to execute unknown command"
        );
        ex.Execute(std::forward<Command>(cmd));
    }

}