#include "Request.hpp"

#include <cassert>
#include <charconv>
#include <stdexcept>
#include <cctype> // std::tolower
#include <array>

namespace {
 
    // case insensetive comparator
    bool IsEqual(std::string_view lhs, std::string_view rhs) noexcept {
        bool isEqual = lhs.size() == rhs.size();
        for (size_t i = 0; isEqual && i < lhs.size(); i++) {
            isEqual = std::tolower(lhs[i]) == std::tolower(rhs[i]); 
        }
        return isEqual;
    }

    constexpr std::string_view Trim(
        std::string_view text, 
        std::string_view exclude = " \n\r\t\v\0"
    ) {
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

    size_t ExtractInteger(std::string_view sequence, size_t radix = 10) {
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

namespace blizzard {

Header ParseHeader(std::string_view src) {
    constexpr auto FIELD_DELIMITER = Header::FIELD_DELIMITER;
    constexpr std::string_view STATUS_DELIMITER = " ";
    Header result{};

    // extract STATUS
    size_t statusEnd = src.find_first_of(FIELD_DELIMITER.data());
    assert(statusEnd != std::string::npos && "Header doesn't have a status");
    std::string_view statusLine { src.data(), statusEnd };
    statusEnd += FIELD_DELIMITER.size();

    std::array<std::string_view, 3> statusParts;
    for (size_t i = 0; i < statusParts.size(); i++) {
        if (auto last = statusLine.find_first_of(STATUS_DELIMITER); 
            last == std::string_view::npos
        ) {
            statusParts[i] = statusLine;
        }
        else {
            statusParts[i] = { statusLine.data(), last };
            statusLine.remove_prefix(last + STATUS_DELIMITER.size());
        }
    }
    for (auto part: statusParts) {
        assert(Trim(part) == part && "Status parsing failed");
    }

    result.m_httpVersion = statusParts[0];
    result.m_statusCode = static_cast<uint16_t>(ExtractInteger(statusParts[1]));
    result.m_reasonPhrase = statusParts[2];
    // default values:
    result.m_bodyKind = BodyContentKind::unknown;
    result.m_bodyLength = std::string_view::npos;
    // extract FIELDS
    for (size_t start = statusEnd, finish = src.find_first_of(FIELD_DELIMITER.data(), statusEnd); 
        finish != std::string::npos && start != finish;
        finish = src.find_first_of(FIELD_DELIMITER.data(), start)
    ) {
        std::string_view raw { src.data() + start, finish - start };
        const auto split = raw.find_first_of(':');
        assert(split != std::string_view::npos && "Expected format: <key>:<value> not found");

        std::string_view key { raw.data(), split };
        raw.remove_prefix(split + 1);
        raw = Trim(raw, " ");

        if (IsEqual(key, Header::CONTENT_LENGTH_KEY)) {
            result.m_bodyKind = BodyContentKind::contentLengthSpecified;
            result.m_bodyLength = ExtractInteger(raw);
        }
        else if (IsEqual(key, Header::TRANSFER_ENCODED_KEY) 
            && IsEqual(raw, Header::TRANSFER_ENCODED_VALUE)         
        ) {
            result.m_bodyKind = BodyContentKind::chunkedTransferEncoded;
            result.m_bodyLength = std::string_view::npos;
        }

        // update start
        start = finish + FIELD_DELIMITER.size();
    }

    return result;
}

}