#pragma once

#include <string_view>
#include <string>
#include <cstddef>

namespace utils {
    
namespace ascii {

/**
 * case insensetive comparator
 */
bool IsEqual(std::string_view lhs, std::string_view rhs) noexcept;

} // namespace ascii {

namespace utf8 {
    
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
bool IsEqual(std::string_view lhs, std::string_view rhs);

} // namespace utf8 

size_t ExtractInteger(std::string_view sequence, int radix = 10);

std::string_view Trim(std::string_view text
    , std::string_view exclude = " \n\r\t\v\0"
) noexcept;

} // namespace utils
