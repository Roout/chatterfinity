#pragma once

#include <string_view>
#include <string>

class Query {
public:
    virtual ~Query() = default;

    virtual std::string Build() const = 0;
};

namespace twitch {
    static constexpr std::string_view kHost = "irc.chat.twitch.tv";
    static constexpr std::string_view kService = "6697";
    
    // Sending user access and app access tokens
    // When an API request requires authentication, send the access token as a header
    class SendToken : public Query {
    public:

        SendToken(const std::string& token)
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

    // validate user access tokens
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

    class IrcAuth : public Query {
    public:

        IrcAuth(const std::string& token, const std::string& user)
            : token_ { token }
            , user_ { user }
        {}

        std::string Build() const override;

    private:
        std::string token_;
        std::string user_;
    };

    class Pong : public Query {
    public:

        Pong() = default;

        std::string Build() const override;
    };

    class Join : public Query {
    public:

        Join(const std::string& channel) 
            : channel_ { channel } {}

        std::string Build() const override;

    private:
        std::string channel_;
    };

    class Chat : public Query {
    public:

        Chat(const std::string& channel, const std::string& message) 
            : channel_ { channel }
            , message_ { message }
        {}

        std::string Build() const override;

    private:
        std::string channel_;
        std::string message_;
    };

    class Leave : public Query {
    public:

        Leave(const std::string& channel) 
            : channel_ { channel } {}

        std::string Build() const override;

    private:
        std::string channel_;
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