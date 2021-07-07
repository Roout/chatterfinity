#pragma once
#include <type_traits>
#include <vector>
#include <string_view>
#include <sstream>
#include <string>
#include <cassert>

namespace command {

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
            assert(!params.empty());
            Shutdown cmd {};
            for (auto it = params.cbegin(); it != params.cend(); ++it) {
                cmd.message_ += std::string(*it) +  " ";
            }
            return cmd;
        }

    private:
        std::string message_;
    };

    namespace details {
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
    }

// Helper functions:
    template<typename Command, typename Executor,
        typename = std::enable_if_t<details::is_blizzard_api_v<Command>>
    >
    void Execute(Command&& cmd, Executor& ex) {
        ex.Execute(std::forward<Command>(cmd));
    }

}