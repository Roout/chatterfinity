#pragma once

#include <string_view>
#include <string>

namespace request {

class Query {
public:
    virtual ~Query() = default;

    virtual std::string Build() const = 0;
};

namespace twitch {
    static constexpr std::string_view kHost = "irc.chat.twitch.tv";
    static constexpr std::string_view kService = "6697";
    
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

    class Ping : public Query {
    public:

        Ping(const std::string& channel) 
            : channel_ { channel } {}

        std::string Build() const override;
        
    private:
        std::string channel_;
    };

    class Pong : public Query {
    public:

        Pong() = default;

        std::string Build() const override;
    };

    class Join : public Query {
    public:

        Join(const std::string& channel);

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

    // > Realm API:
    // > Realm
    // > Returns a single realm by slug or ID.
    class Realm : public Query {
    public:

        Realm(const std::string& token)
            : token_ { token }
        {}

        std::string Build() const override;

    private:
        std::string token_;
    };

    // > Connected Realm API: 
    // > Connected Realm
    // > Realm Status
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
    class Arena : public Query {
    public:

        Arena(std::uint64_t season
            , std::uint64_t teamSize
            , const std::string& token
        )   
            : region_ { 0 } // TODO: I don't know what it means but it was added to API
            , season_ { season }
            , teamSize_ { teamSize }
            , token_ { token }
        {}

        std::string Build() const override;

    private:
        std::uint64_t region_;
        std::uint64_t season_;
        std::uint64_t teamSize_;
        std::string token_;
    };
    // 6. Auction
}

} // namespace request