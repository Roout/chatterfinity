#pragma once
#include <type_traits>

namespace command {

    struct RealmID {};
    struct RealmStatus {};
    struct AccessToken {};
    struct Quit {};

// Type traits:
    template<typename T>
    struct is_blizzard_api {
        static constexpr bool value { 
            std::is_same_v<T, RealmID>
            || std::is_same_v<T, RealmStatus>
            || std::is_same_v<T, AccessToken>
            || std::is_same_v<T, Quit>
        };
    };

    template<typename T>
    constexpr bool is_blizzard_api_v = is_blizzard_api<T>::value;

// Helper functions:
    template<typename Command, typename Executor,
        typename = std::enable_if_t<is_blizzard_api_v<Command>>
    >
    auto Execute(Command&& cmd, Executor& ex) {
        return ex.Execute(std::forward<Command>(cmd));
    }

}