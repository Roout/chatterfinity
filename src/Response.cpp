#include "Response.hpp"
#include "Utility.hpp"

#include <cassert>
#include <array>

namespace net::http {

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
        assert(utils::Trim(part) == part && "Status parsing failed");
    }

    result.m_httpVersion = statusParts[0];
    result.m_statusCode = static_cast<uint16_t>(utils::ExtractInteger(statusParts[1]));
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
        raw = utils::Trim(raw, " ");

        if (utils::IsEqual(key, Header::CONTENT_LENGTH_KEY)) {
            result.m_bodyKind = BodyContentKind::contentLengthSpecified;
            result.m_bodyLength = utils::ExtractInteger(raw);
        }
        else if (utils::IsEqual(key, Header::TRANSFER_ENCODED_KEY) 
            && utils::IsEqual(raw, Header::TRANSFER_ENCODED_VALUE)         
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