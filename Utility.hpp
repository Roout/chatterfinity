#pragma once

#include <string_view>
#include <string>
#include <charconv>
#include <stdexcept>
#include <type_traits>
#include <cstdint>

namespace utils {
    
    inline size_t ExtractInteger(std::string_view sequence, size_t radix = 10) {
        using namespace std::literals;
        size_t value;
        const auto [parsed, ec] = std::from_chars(std::data(sequence)
            , std::data(sequence) + std::size(sequence)
            , value
            , radix);
        if (ec != std::errc()) {
            auto message = "std::from_chars met unexpected input.\n\tBefore parsing: "s 
                + std::string{ sequence.data(), sequence.size() } 
                + "\n\tAfter parsing: "s 
                + parsed;
            throw std::logic_error(message);
        }
        return value;
    }
    
}

namespace enums {

    template<typename Enum, typename ...States>
    constexpr bool contains_any_of(std::uint64_t mask, States ...other) noexcept {
        static_assert((std::is_same_v<States, Enum> && ...), 
            "Fail to instantiate "
            "constexpr bool contains(std::uint64_t mask, States ...other) noexcept "
            "method. Expected same template types, e.g., "
            "contains(State::A, State::B, State::C) "
            "where State is enum type"
        );
        using buildin_t = std::underlying_type_t<Enum>;

        const auto lhs = (static_cast<buildin_t>(other) | ...);
        const auto rhs = static_cast<buildin_t>(mask);
        return lhs & rhs;
    }

    template<typename Enum, typename ...States>
    constexpr auto merge(States ...states) noexcept {
        static_assert((std::is_same_v<States, Enum> && ...), 
            "Fail to instantiate "
            "constexpr auto merge(States ...states) noexcept "
            "method. Expected same template types, e.g., "
            "contains<State>(State::A, State::B, State::C) "
            "where State is enumeration type"
        );
        using buildin_t = std::underlying_type_t<Enum>;
        return (static_cast<buildin_t>(states) | ...);
    }

    template<typename Enum>
    constexpr std::uint64_t toggle(std::uint64_t mask, Enum state) noexcept {
        return mask ^ static_cast<std::uint64_t>(state);
    }

}