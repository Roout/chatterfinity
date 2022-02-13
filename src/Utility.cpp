#include "Utility.hpp"

#include <charconv>     // std::from_chars
#include <stdexcept>    // std::logic_error
#include <cctype>       // std::tolower
#include <cwchar>       // std::mbrtowc
#include <locale>

namespace utils {

namespace ascii {

// case insensetive comparator
bool IsEqual(std::string_view lhs, std::string_view rhs) noexcept {
    bool isEqual = lhs.size() == rhs.size();
    for (size_t i = 0; isEqual && i < lhs.size(); i++) {
        isEqual = std::tolower(lhs[i]) == std::tolower(rhs[i]); 
    }
    return isEqual;
}

} // namespace ascii

namespace utf8 {

bool IsEqual(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    if (lhs.empty()) return true;
    
    /**
     * If the next `n` bytes constitute an incomplete, 
     * but so far valid, multibyte character. 
     * Nothing is written to the location 
     * where the resulting wide character will be written
     * 
     * Encoding error: static_cast<std::size_t>(-1) has greater 
     * value than incomplete error
    */
    constexpr auto kIncompleteError = static_cast<size_t>(-2);

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
        
        if (!leftBytes || leftBytes >= kIncompleteError) break;
        if (!rightBytes || rightBytes >= kIncompleteError) break;

        left.src.remove_prefix(leftBytes);
        right.src.remove_prefix(rightBytes);

        isEqual = isEqual && (
            std::tolower(leftChar, loc) == std::tolower(rightChar, loc)
        );
    }
    return isEqual;
}

} // namespace utf8

size_t ExtractInteger(std::string_view sequence, int radix) {
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

std::string_view Trim(std::string_view text
    , std::string_view exclude
) noexcept {
    if (const size_t leftShift = text.find_first_not_of(exclude); 
        leftShift != std::string_view::npos
    ) {
        text.remove_prefix(leftShift);
    }
    else {
        return {};
    }

    if (const size_t rightShift = text.find_last_not_of(exclude);
        rightShift != std::string_view::npos
    ) {
        text.remove_suffix(text.size() - rightShift - 1);
    }
    else {
        return {};
    }
    
    return text;
}

} // namespace utils