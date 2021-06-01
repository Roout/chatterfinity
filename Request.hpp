#pragma once
#include <string_view>
#include <string>

namespace blizzard {

    constexpr std::string_view REGION = "eu";
    constexpr std::string_view LOCALE = "en_US";
    constexpr std::string_view NAMESPACE = "dynamic-classic-eu";
    constexpr std::string_view SERVER_SLUG = "flamegor";

    class Query {
    public:
        virtual ~Query() = default;

        virtual std::string Build() const = 0;
    };

    /*
     * 1. Get Access Token
     * in: client_id, client_secret
     * out: access_token, token_type, expires_in
     **/
    class ExchangeCredentials : public Query {
    public:
        ExchangeCredentials() {
            m_id = "a07bb90a99014de29167b44a72e7ca36";
            m_secret = "frDE4uJuZyc4mz5EOayle2dNJo1BK13O";
        }

        std::string Build() const override;

    private:
        // client credentials
        std::string m_id;
        std::string m_secret;
    };
    // 2. Server Status
    // 3. Creature Families { wolf, ... }
    // 4. Creature Types { Beast, Giant, ... }
    // 5. Arena
    // 6. Auction
}