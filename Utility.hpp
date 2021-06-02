#pragma once

#include <string_view>
#include <string>
#include <charconv>
#include <stdexcept>

namespace Utils {
    
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