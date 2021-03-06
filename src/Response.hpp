// [RFC 2616 Response status line](https://datatracker.ietf.org/doc/html/rfc2616#section-6.1.1)
// -> More about headers:
// [Representation Header](https://developer.mozilla.org/en-US/docs/Glossary/Representation_header)
// [Multiple-resource bodies](https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types#multipartform-data)
// [RFC 7230 Message transfer, body length etc](https://datatracker.ietf.org/doc/html/rfc7230#section-3.3.3)
// -> IRC:
// [RFC 1459 IRC](https://datatracker.ietf.org/doc/html/rfc1459.html#section-2.1)
#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <cstdint>
#include <array>
#include <optional>

namespace net {

    namespace http {

        enum class BodyContentKind : std::uint16_t {
            kChunkedTransferEncoded,
            kContentLengthSpecified,
            kUnknown // may be Multiple-resource bodies
        };
        
        struct Header {    
            static constexpr std::string_view kFieldDelimiter = "\r\n";
            static constexpr std::string_view kHeadDelimiter = "\r\n\r\n";
            static constexpr std::string_view kTransferEncodedKey = "transfer-encoding";
            static constexpr std::string_view kTransferEncodedValue = "chunked";
            static constexpr std::string_view kContentLengthKey = "content-length";

            // status line
            std::string     httpVersion_;
            std::string     reasonPhrase_;
            std::uint16_t   statusCode_;

            // can't get rid of this property cuz it's important to confirm 
            // it's not a `BodyContentKind::unknown` type
            BodyContentKind bodyKind_;
            std::uint64_t   bodyLength_;
        };

        Header ParseHeader(std::string_view src);
        
        using Body = std::string;

        struct Message {
            Header  header_;
            Body    body_;
        };
        
    }

    namespace irc {

        // [IRC](https://datatracker.ietf.org/doc/html/rfc1459.html#section-2.1)
        struct Message {
            struct Tag {
                std::string key_;
                std::string value_;
            };
            // The prefix, command, and all parameters are
            // separated by one (or more) ASCII space character(s) (0x20).
            static constexpr char kSpace = ' ';
            static constexpr std::string_view kCRLF = "\r\n";
            
            std::vector<Tag> tags_;
            std::string prefix_;
            std::string command_;
            std::vector<std::string> params_;
        };

        Message ParseMessage(std::string_view src);

        class IrcCommands final {
        public:
            enum CommandKind: size_t {
                kPrivMsg,
                kPing,
                kCount
            };

            constexpr IrcCommands() 
                : commands_ { "PRIVMSG", "PING" }
            {}

            constexpr std::string_view Get(CommandKind kind) const noexcept {
                return commands_[kind];
            }

            constexpr std::optional<CommandKind> Get(
                    std::string_view cmd) const noexcept 
            {
                for (size_t i = 0; i < kCount; i++) {
                    if (commands_[i] == cmd) {
                        return { CommandKind{ i } };
                    }
                }
                return std::nullopt;
            }

        private:
            const std::array<std::string_view, kCount> commands_;
        };

    }

} // namespace net
