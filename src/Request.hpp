#pragma once

#include <string_view>
#include <string>

class Query {
public:
    virtual ~Query() = default;

    virtual std::string Build() const = 0;
};

namespace twitch {
    static constexpr std::string_view   kHost = "irc.chat.twitch.tv";
    static constexpr std::uint16_t      kPort = 6697;
    
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

    // App Access token (Credential User Flow)
    class Authentication : public Query {
    public:

        Authentication(const std::string& token)
            : token_ { token }
        {}

        std::string Build() const override;

    private:
        std::string token_;
    };

    class TokenRevoke : public Query {
    public:

        TokenRevoke(const std::string& id, const std::string& token)
            : id_ { id }
            , token_ { token }
        {}

        std::string Build() const override;

    private:
        std::string id_;
        std::string token_;
    };

    // validate App access tokens
    // https://dev.twitch.tv/docs/authentication#getting-tokens
    // https://dev.twitch.tv/docs/authentication#validating-requests
    class Validation : public Query {
    public:

        Validation(const std::string& token)
            : token_ { token }
        {}

        std::string Build() const override;

    private:
        std::string token_;
    };
}

namespace blizzard {

    static constexpr std::string_view kServerSlug   = "flamegor";
    static constexpr std::string_view kNamespace    = "dynamic-classic-eu";
    static constexpr std::string_view kLocale       = "en_US";
    static constexpr std::string_view kRegion       = "eu";

    static_assert(
        kNamespace[kNamespace.size() - 2] == kRegion[0] && kNamespace.back() == kRegion.back()
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