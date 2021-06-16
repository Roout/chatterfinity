#pragma once

#include <string_view>
#include <string>

namespace blizzard {

    constexpr std::string_view SERVER_SLUG  = "flamegor";
    constexpr std::string_view NAMESPACE    = "dynamic-classic-eu";
    constexpr std::string_view LOCALE       = "en_US";
    constexpr std::string_view REGION       = "eu";

    static_assert(
        NAMESPACE[NAMESPACE.size() - 2] == REGION[0] && NAMESPACE.back() == REGION.back()
        , "Region mismatch"
    );

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
    class CredentialsExchange : public Query {
    public:
        CredentialsExchange(const std::string& id, const std::string& secret) 
            : m_id { id }
            , m_secret { secret }
        {
        }

        std::string Build() const override;

    private:
        // client credentials
        std::string m_id;
        std::string m_secret;
    };

    // Realm API: Realm by slug
    class Realm : public Query {
    public:

        Realm(const std::string& token)
            : m_token { token }
        {}

        std::string Build() const override;

    private:
        std::string m_token;
    };

    // 2. Connected Realm API: Connected Realm
    // Realm Status
    class RealmStatus : public Query {
    public:

        RealmStatus(std::uint64_t realmId, const std::string& token)
            : m_connetedRealmId { realmId }
            , m_token { token }
        {}

        std::string Build() const override;

    private:
        std::uint64_t m_connetedRealmId;
        std::string m_token;
    };

    // 3. Creature Families { wolf, ... }
    // 4. Creature Types { Beast, Giant, ... }
    // 5. Arena
    // 6. Auction
}