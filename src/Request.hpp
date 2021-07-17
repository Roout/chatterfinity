#pragma once

#include <string_view>
#include <string>

class Query {
public:
    virtual ~Query() = default;

    virtual std::string Build() const = 0;
};

namespace twitch {
    /*
     * 1. Get Access Token
     * in: client_id, client_secret
     * out: access_token, token_type, expires_in
     **/
    class CredentialsExchange : public Query {
    public:
        CredentialsExchange(const std::string& id, const std::string& secret) 
            : id_ { id }
            , secret_ { secret }
        {
        }

        std::string Build() const override;

    private:
        // client credentials
        std::string id_;
        std::string secret_;
    };

}

namespace blizzard {

    constexpr std::string_view SERVER_SLUG  = "flamegor";
    constexpr std::string_view NAMESPACE    = "dynamic-classic-eu";
    constexpr std::string_view LOCALE       = "en_US";
    constexpr std::string_view REGION       = "eu";

    static_assert(
        NAMESPACE[NAMESPACE.size() - 2] == REGION[0] && NAMESPACE.back() == REGION.back()
        , "Region mismatch"
    );

    /*
     * 1. Get Access Token
     * in: client_id, client_secret
     * out: access_token, token_type, expires_in
     **/
    class CredentialsExchange : public Query {
    public:
        CredentialsExchange(const std::string& id, const std::string& secret) 
            : id_ { id }
            , secret_ { secret }
        {
        }

        std::string Build() const override;

    private:
        // client credentials
        std::string id_;
        std::string secret_;
    };

    // Realm API: Realm by slug
    class Realm : public Query {
    public:

        Realm(const std::string& token)
            : token_ { token }
        {}

        std::string Build() const override;

    private:
        std::string token_;
    };

    // 2. Connected Realm API: Connected Realm
    // Realm Status
    class RealmStatus : public Query {
    public:

        RealmStatus(std::uint64_t realmId, const std::string& token)
            : connetedRealmId_ { realmId }
            , token_ { token }
        {}

        std::string Build() const override;

    private:
        std::uint64_t connetedRealmId_;
        std::string token_;
    };

    // 3. Creature Families { wolf, ... }
    // 4. Creature Types { Beast, Giant, ... }
    // 5. Arena
    // 6. Auction
}