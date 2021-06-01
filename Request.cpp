#include "Request.hpp"
#include <string>
#include <string_view>
#include <cstdint>
#include <cassert>

#include <boost/format.hpp>

namespace {
    
    // [RFC 4648 Base64 encoding](https://datatracker.ietf.org/doc/html/rfc4648#section-5)
    class UrlBase64 {
    public:
        
        static std::string Encode(std::string_view src) {
            const auto length = 4 * ((src.size() + 2) / 3);
            const auto lastOctets = src.size() % 3;
            std::string result;
            result.reserve(length);
            for (size_t i = 0; i + lastOctets < src.size(); i += 3) {
                const uint32_t merged = (src[i] << 16) | (src[i + 1] << 8) | src[i + 2];
                result.push_back(TABLE[(merged >> 18) & 0x3F]);
                result.push_back(TABLE[(merged >> 12) & 0x3F]);
                result.push_back(TABLE[(merged >> 6 ) & 0x3F]);
                result.push_back(TABLE[(merged >> 0 ) & 0x3F]);
            }
            if (lastOctets == 1) {
                /*
                (2) The final quantum of encoding input is exactly 8 bits; here, the
                    final unit of encoded output will be two characters followed by
                    two "=" padding characters.
                */
                result.push_back(TABLE[(src.back() >> 2) & 0x3F]);
                result.push_back(TABLE[(src.back() << 4) & 0x3F]);
                result.append("==");
            }
            else if (lastOctets == 2) {
                /*
                (3) The final quantum of encoding input is exactly 16 bits; here, the
                    final unit of encoded output will be three characters followed by
                    one "=" padding character.
                */
                const uint32_t merged = (src[src.size() - 2] << 16) | (src.back() << 8);
                result.push_back(TABLE[(merged >> 18) & 0x3F]);
                result.push_back(TABLE[(merged >> 12) & 0x3F]);
                result.push_back(TABLE[(merged >> 6 ) & 0x3F]);
                result.push_back('=');
            }
            return result;
        }
    private:
        static constexpr std::string_view TABLE 
            = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=";
        static_assert(TABLE.size() == 65);
    };
}

namespace blizzard {

std::string ExchangeCredentials::Build() const {
    const char *request = 
        "POST /oauth/token HTTP/1.1\r\n"
        "Host: eu.battle.net\r\n"                
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Authorization: Basic %1%\r\n"
        "Content-Length: 29\r\n"
        "\r\n"
        "grant_type=client_credentials";
    
    return (boost::format(request) % UrlBase64::Encode(m_id + ':' + m_secret)).str();
}

}
