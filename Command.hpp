#pragma once
#include <type_traits>

namespace command {
    struct RealmID {
         
        template<typename Executor>
        void accept(Executor& executor) {
            executor.Execute(*this);
        }
    };

    struct RealmStatus {
         
        template<typename Executor>
        void accept(Executor& executor) {
            executor.Execute(*this);
        }
    };

    struct AccessToken {
         
        template<typename Executor>
        void accept(Executor& executor) {
            executor.Execute(*this);
        }
    };


// Type traits:
    template<typename T>
    struct is_blizzard_api {
        static constexpr bool value { 
            std::is_same_v<T, RealmID>
            || std::is_same_v<T, RealmStatus>
            || std::is_same_v<T, AccessToken>
        };
    };



}