#include "Command.hpp"

#include "Twitch.hpp"
// #include "Blizzard.hpp"
// #include "Console.hpp"

namespace command {

    Login Login::Create(const service::Twitch& ctx, const Params& params) {
        if (params.size() == 2) {
            // user + token
            return { std::string(params[0]), std::string(params[1]) };
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

}