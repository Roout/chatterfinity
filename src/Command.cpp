#include "Command.hpp"
#include "Utility.hpp"

// services (used as context for commands)
#include "Twitch.hpp"
#include "Blizzard.hpp"
#include "Console.hpp"

namespace command {

    RealmStatus RealmStatus::Create(const service::Blizzard& ctx,
        const Params& params
    ) {
        if (params.size() == 2) {
            std::string channel { params[0] };
            std::string initiator { params[1] };
            return { std::move(channel), std::move(initiator) };
        }
        return {};
    }

    RealmStatus RealmStatus::Create(const service::Twitch& ctx,
        const Params& params
    ) {
        if (params.size() == 2) {
            std::string channel { params[0] };
            std::string initiator { params[1] };
            return { std::move(channel), std::move(initiator) };
        }
        return {};
    }

    Login Login::Create(const service::Twitch& ctx, const Params& params) {
        if (params.size() == 2) {
            std::string user { params[0] };
            std::string token { params[1] };
            return { std::move(user), std::move(token) };
        }
        else if (params.size() <= 1) {
            // get from token
            auto secret = ctx.GetConfig()->GetSecret("twitch");
            return { std::move(secret->user_), std::move(secret->token_) };
        }
        else { // 0 or > 2
            // TODO: raise exception
            return {};
        }
        
    }

    Join Join::Create(const service::Twitch& ctx, const Params& params) {
        if (params.size() == 1) {
            return { std::string(params.front()) };
        }
        // TODO: raise exception
        return {};
    }

    Leave Leave::Create(const service::Twitch& ctx, const Params& params) {
        if (params.size() == 1) {
            return { std::string(params.front()) };
        }
        // TODO: raise exception
        return {};
    }

    Chat Chat::Create(const service::Twitch& ctx, const Params& params) {
        if (params.size() == 2) {
            std::string channel { params[0] };
            std::string message { params[1] };
            // channel, meesage
            return { std::move(channel), std::move(message) };
        }
        // TODO: raise exception
        return {};
    }

}