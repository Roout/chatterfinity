#pragma once
#include <cstddef>
#include <type_traits>
#include <optional>
#include <string_view>
#include <array>

namespace service {

    // coupling enum value with string_view in both ways
    class Couple final {
    public:

        enum ServiceKind {
            kBlizzard,
            kTwitch,
            kConsole,
            kCount
        };

        constexpr Couple() 
            : services_ { "blizzard", "service", "twitch" }
        {}

        constexpr std::string_view Get(ServiceKind kind) const noexcept {
            return services_[kind];
        }

        constexpr std::optional<ServiceKind> Get(
                std::string_view cmd) const noexcept 
        {
            for (size_t i = 0; i < kCount; i++) {
                if (services_[i] == cmd) {
                    return { static_cast<ServiceKind>(i) };
                }
            }
            return std::nullopt;
        }

    private:
        const std::array<std::string_view, kCount> services_;
    };

} // namespace service

namespace cst {

constexpr std::size_t kMaxIrcMessageSize { 500 };
constexpr std::size_t kQueueCapacity { 255 };
inline constexpr service::Couple kServiceCouple {};

} // namespace cst
