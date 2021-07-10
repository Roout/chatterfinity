// [RFC 2616 Response status line](https://datatracker.ietf.org/doc/html/rfc2616#section-6.1.1)
// -> More about headers:
// [Representation Header](https://developer.mozilla.org/en-US/docs/Glossary/Representation_header)
// [Multiple-resource bodies](https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types#multipartform-data)
// [RFC 7230 Message transfer, body length etc](https://datatracker.ietf.org/doc/html/rfc7230#section-3.3.3)
#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace net::http {

    enum class BodyContentKind : std::uint16_t {
        chunkedTransferEncoded,
        contentLengthSpecified,
        unknown // may be Multiple-resource bodies
    };
    
    struct Header {    
        static constexpr std::string_view FIELD_DELIMITER = "\r\n";
        static constexpr std::string_view HEAD_DELIMITER = "\r\n\r\n";
        static constexpr std::string_view TRANSFER_ENCODED_KEY = "transfer-encoding";
        static constexpr std::string_view TRANSFER_ENCODED_VALUE = "chunked";
        static constexpr std::string_view CONTENT_LENGTH_KEY = "content-length";

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
