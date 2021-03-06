#include "Request.hpp"
#include <cctype> // std::tolower
#include <string>
#include <string_view>
#include <cstdint>
#include <cassert>
#include <algorithm>

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
                result.push_back(kTable[(merged >> 18) & 0x3F]);
                result.push_back(kTable[(merged >> 12) & 0x3F]);
                result.push_back(kTable[(merged >> 6 ) & 0x3F]);
                result.push_back(kTable[(merged >> 0 ) & 0x3F]);
            }
            if (lastOctets == 1) {
                /*
                (2) The final quantum of encoding input is exactly 8 bits; here, the
                    final unit of encoded output will be two characters followed by
                    two "=" padding characters.
                */
                result.push_back(kTable[(src.back() >> 2) & 0x3F]);
                result.push_back(kTable[(src.back() << 4) & 0x3F]);
                result.append("==");
            }
            else if (lastOctets == 2) {
                /*
                (3) The final quantum of encoding input is exactly 16 bits; here, the
                    final unit of encoded output will be three characters followed by
                    one "=" padding character.
                */
                const uint32_t merged = (src[src.size() - 2] << 16) | (src.back() << 8);
                result.push_back(kTable[(merged >> 18) & 0x3F]);
                result.push_back(kTable[(merged >> 12) & 0x3F]);
                result.push_back(kTable[(merged >> 6 ) & 0x3F]);
                result.push_back('=');
            }
            return result;
        }
    private:
        static constexpr std::string_view kTable 
            = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_=";
        static_assert(kTable.size() == 65);
    };
}

namespace request {

namespace twitch {

std::string Validation::Build() const {
    const char *requestTemplate = 
        "GET /oauth2/validate HTTP/1.1\r\n"
        "Host: id.twitch.tv\r\n"
        "Authorization: OAuth %1%\r\n"
        "\r\n";
    
    return (boost::format(requestTemplate) % token_).str();
}

/*
< PING <channel>
> :tmi.twitch.tv PONG tmi.twitch.tv :<channel>
*/
std::string Ping::Build() const {
    using namespace std::literals;
    return "PING "s + channel_ + "\r\n"s;
}

std::string Pong::Build() const {
    return "PONG :tmi.twitch.tv\r\n";
}

std::string Chat::Build() const {
    const char *requestTemplate = "PRIVMSG #%1% :%2%\r\n";
    return (boost::format(requestTemplate) % channel_ % message_).str();
}

std::string Leave::Build() const {
    // https://datatracker.ietf.org/doc/html/rfc1459.html#section-1.3
    const char *requestTemplate = "PART #%1%\r\n";
    return (boost::format(requestTemplate) % channel_).str();
}

Join::Join(const std::string& channel)
    : channel_ { channel }
{
    std::transform(channel_.cbegin(), channel_.cend(), channel_.begin()
        , [](unsigned char ch) { return std::tolower(ch); });
}

std::string Join::Build() const {
    // https://datatracker.ietf.org/doc/html/rfc1459.html#section-1.3
    const char *requestTemplate = "JOIN #%1%\r\n";
    return (boost::format(requestTemplate) % channel_).str();
}

std::string IrcAuth::Build() const {
    const char *requestTemplate = 
        "CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands\r\n"
        "PASS oauth:%1%\r\n"
        "NICK %2%\r\n";
    return (boost::format(requestTemplate) % token_ % user_).str();
};

} // namespace twitch

namespace blizzard {

std::string CredentialsExchange::Build() const {
    const char *requestTemplate = 
        "POST /oauth/token HTTP/1.1\r\n"
        "Host: %1%.battle.net\r\n"                
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Authorization: Basic %2%\r\n"
        "Content-Length: 29\r\n"
        "\r\n"
        "grant_type=client_credentials";
    
    return (boost::format(requestTemplate) 
        % kRegion 
        % UrlBase64::Encode(id_ + ':' + secret_)
    ).str();
}

std::string Realm::Build() const {
    // https://eu.api.blizzard.com/data/wow/realm/flamegor?namespace=dynamic-classic-eu&locale=en_US&access_token=USJa4cAz4h0Ggw2r07wbM1IfdAIWKFAVhr
    const char *requestTemplate = 
            "GET /data/wow/realm/%1%?namespace=%2%&locale=%3% HTTP/1.1\r\n"
            "Host: %4%.api.blizzard.com\r\n"
            "Authorization: Bearer %5%\r\n"
            "\r\n";
    
    return (boost::format(requestTemplate) 
        % kServerSlug 
        % kNamespace
        % kLocale
        % kRegion
        % token_
    ).str();
}

std::string RealmStatus::Build() const {
    const char *requestTemplate = 
            "GET /data/wow/connected-realm/%1%?namespace=%2%&locale=%3% HTTP/1.1\r\n"
            "Host: %4%.api.blizzard.com\r\n"
            "Authorization: Bearer %5%\r\n"
            "\r\n";
    
    return (boost::format(requestTemplate) 
        % connetedRealmId_
        % kNamespace
        % kLocale
        % kRegion
        % token_
    ).str();
}

std::string Arena::Build() const {
    assert(teamSize_ == 2 || teamSize_ == 3 || teamSize_ == 5);
    
    const char *requestTemplate = 
            "GET /data/wow/pvp-region/%1%/pvp-season/%2%/pvp-leaderboard/%3%v%3%?namespace=%4%&locale=%5% HTTP/1.1\r\n"
            "Host: %6%.api.blizzard.com\r\n"
            "Authorization: Bearer %7%\r\n"
            "\r\n";
    
    return (boost::format(requestTemplate) 
        % region_
        % season_
        % teamSize_
        % kNamespace
        % kLocale
        % kRegion
        % token_
    ).str();
}

}

} // namespace request