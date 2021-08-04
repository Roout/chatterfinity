#include "Response.hpp"
#include "Utility.hpp"

#include <cassert>
#include <array>
#include <algorithm>

namespace net {

namespace http {

Header ParseHeader(std::string_view src) {
    constexpr auto kFieldDelimiter = Header::kFieldDelimiter;
    constexpr std::string_view kStatusDelimiter = " ";
    Header result{};

    // extract STATUS
    size_t statusEnd = src.find_first_of(kFieldDelimiter.data());
    assert(statusEnd != std::string::npos && "Header doesn't have a status");
    std::string_view statusLine { src.data(), statusEnd };
    statusEnd += kFieldDelimiter.size();

    std::array<std::string_view, 3> statusParts;
    for (size_t i = 0; i < statusParts.size(); i++) {
        if (auto last = statusLine.find_first_of(kStatusDelimiter); 
            last == std::string_view::npos
        ) {
            statusParts[i] = statusLine;
        }
        else {
            statusParts[i] = { statusLine.data(), last };
            statusLine.remove_prefix(last + kStatusDelimiter.size());
        }
    }
    for ([[maybe_unused]] auto part: statusParts) {
        assert(utils::Trim(part) == part && "Status parsing failed");
    }

    result.httpVersion_ = statusParts[0];
    result.statusCode_ = static_cast<uint16_t>(utils::ExtractInteger(statusParts[1]));
    result.reasonPhrase_ = statusParts[2];
    // default values:
    result.bodyKind_ = BodyContentKind::kUnknown;
    result.bodyLength_ = std::string_view::npos;
    // extract FIELDS
    for (size_t start = statusEnd, finish = src.find_first_of(kFieldDelimiter.data(), statusEnd); 
        finish != std::string::npos && start != finish;
        finish = src.find_first_of(kFieldDelimiter.data(), start)
    ) {
        std::string_view raw { src.data() + start, finish - start };
        const auto split = raw.find_first_of(':');
        assert(split != std::string_view::npos && "Expected format: <key>:<value> not found");

        std::string_view key { raw.data(), split };
        raw.remove_prefix(split + 1);
        raw = utils::Trim(raw, " ");

        if (utils::IsEqual(key, Header::kContentLengthKey)) {
            result.bodyKind_ = BodyContentKind::kContentLengthSpecified;
            result.bodyLength_ = utils::ExtractInteger(raw);
        }
        else if (utils::IsEqual(key, Header::kTransferEncodedKey) 
            && utils::IsEqual(raw, Header::kTransferEncodedValue)         
        ) {
            result.bodyKind_ = BodyContentKind::kChunkedTransferEncoded;
            result.bodyLength_ = std::string_view::npos;
        }

        // update start
        start = finish + kFieldDelimiter.size();
    }

    return result;
}

} // namespace http

namespace irc {

Message ParseMessage(std::string_view src) {
    Message message {};
    src = utils::Trim(src);   
    assert(!src.empty());
    // extract tags
    // @badge-info=;badges=;color=;display-name=chatterfinity;
    // emote-sets=0;user-id=713654970;user-type= :tmi.twitch.tv GLOBALUSERSTATE
    if (src.front() == '@') {
        src.remove_prefix(1);
        constexpr std::string_view kTagDelimiter { "; " };
        constexpr char kKeyDelimiter { '=' };
        while (src.front() != ':') { 
            Message::Tag tag;
            if (auto tagDelim = src.find_first_of(kTagDelimiter); 
                tagDelim != std::string_view::npos
            ) {
                if (auto keyDelim = src.find_first_of(kKeyDelimiter);
                    keyDelim != std::string_view::npos
                ) {
                    tag.key_.assign(src.data(), keyDelim);
                    tag.value_.assign(src.data() + keyDelim + 1, tagDelim - keyDelim - 1);
                    src.remove_prefix(tagDelim + 1);
                }
                else {
                    assert(false && "Unexpected IRC v3 Tag Message Format");
                }
            }
            else {
                assert(false && "Unexpected IRC v3 Tag Message Format:"
                    "absent tags-prefix delimiter");
            }
            assert(!src.empty() && "Unexpected IRC v3 Tag Message Format");
            message.tags_.emplace_back(std::move(tag));
        }
    }
    src = utils::Trim(src);
    // extract prefix
    // <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
    if (src.front() == ':') {
        if (auto separator = src.find_first_of(Message::kSpace); 
            separator != std::string_view::npos
        ) {
            // extract prefix (':',[prefix],' ')
            message.prefix_ = src.substr(1, separator - 1);
            src.remove_prefix(separator + 1);
            src = utils::Trim(src);
        }
        else {
            assert(false && "wrong IRC message format: has ':' but no prefix");
        }
    }

    // extract command (command,' ')
    // <letter> { <letter> } | <number> <number> <number>
    if (auto separator = src.find_first_of(" "); 
        separator != std::string_view::npos
    ) {
        message.command_ = src.substr(0, separator);
        src.remove_prefix(separator + 1);
        src = utils::Trim(src);
        // TODO: add syntax check whether command is valid or not
    }
    else {
        // There were no params after the command
        message.command_ = src;
        src= {};
    }

    // parse params:
    // <params>   ::= <SPACE> [ ':' <trailing> | <middle> <params> ]
    constexpr size_t kMaxParams { 15 };
    message.params_.reserve(kMaxParams);
    while (!src.empty()) {
        if (src.front() == ':') {
            // <trailing>
            src.remove_prefix(1);
            src = utils::Trim(src);
            message.params_.emplace_back(src);
            src = {};
        }
        else {
            // <middle>
            if(auto separator = src.find_first_of(Message::kSpace); 
                separator != std::string_view::npos
            ) {
                message.params_.emplace_back(src.substr(0, separator));
                src.remove_prefix(separator + 1);
                src = utils::Trim(src);
            }
            else {
                message.params_.emplace_back(src);
                // empty trailing
                src = {};
            }
        }
    }

    assert(message.params_.size() <= kMaxParams && "wrong IRC message format: to many params");

    return message;
}

} // namespace irc

} // namespace net