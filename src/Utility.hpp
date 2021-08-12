#pragma once

#include <string_view>
#include <string>
#include <charconv>
#include <stdexcept>
#include <type_traits>
#include <cstdint>
#include <cctype> // std::tolower
#include <locale>

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
    
    // case insensetive comparator
    inline bool IsEqual(std::string_view lhs, std::string_view rhs) noexcept {
        bool isEqual = lhs.size() == rhs.size();
        for (size_t i = 0; isEqual && i < lhs.size(); i++) {
            isEqual = std::tolower(lhs[i]) == std::tolower(rhs[i]); 
        }
        return isEqual;
    }

    /**
     * Compare for case insensitive equality of two utf8 byte strings.
     * 
     * Uses std::tolower with en_US.UTF-8 locale which
     * returns the lowercase form of ch if one is listed in the locale, 
     * otherwise return ch unchanged.
     * 
     * @note Only 1:1 character mapping can be performed by this function, 
     * e.g. the Greek uppercase letter 'Σ' has two lowercase forms, 
     * depending on the position in a word: 'σ' and 'ς'
    */
    inline bool IsEqualUtf8(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) return false;
        if (lhs.empty()) return true;
        
        struct State {
            std::string_view src; // source of input
            std::mbstate_t state; // unique state required for `std::mbrtowc`

            State(std::string_view arg) noexcept
                : src { arg }
                , state { std::mbstate_t() }
            {}
        };

        static std::locale loc{ "en_US.UTF-8" };
        State left{ lhs }, right{ rhs };
        wchar_t leftChar, rightChar;
        auto isEqual { true };
        while (isEqual) {
            const auto leftBytes = std::mbrtowc(&leftChar, left.src.data()
                , left.src.size(), &left.state);
            const auto rightBytes = std::mbrtowc(&rightChar, right.src.data()
                , right.src.size(), &right.state);
            isEqual = leftBytes == rightBytes;
            
            if (!leftBytes || leftBytes >= static_cast<size_t>(-2)) break;
            if (!rightBytes || rightBytes >= static_cast<size_t>(-2)) break;

            left.src.remove_prefix(leftBytes);
            right.src.remove_prefix(rightBytes);

            isEqual = isEqual && (
                std::tolower(leftChar, loc) == std::tolower(rightChar, loc)
            );
        }
        return isEqual;
    }

    inline std::string AsLowerCase(std::string str) noexcept {
        for (auto&c: str) c = std::tolower(c);
        return str;
    }

    constexpr std::string_view Trim(
        std::string_view text, 
        std::string_view exclude = " \n\r\t\v\0"
    ) noexcept {
        if (const size_t leftShift = text.find_first_not_of(exclude); leftShift != std::string_view::npos)
            text.remove_prefix(leftShift); 
        else 
            return {};

        if (const size_t rightShift = text.find_last_not_of(exclude); rightShift != std::string_view::npos)
            text.remove_suffix(text.size() - rightShift - 1); 
        else 
            return {};
        
        return text;
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