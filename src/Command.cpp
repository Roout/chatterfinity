#include "Command.hpp"
#include "Utility.hpp"

// services (used as context for commands)
#include "Twitch.hpp"
#include "Blizzard.hpp"
#include "Console.hpp"

#include <algorithm>

namespace {

std::string Find(const command::Args& args, std::string_view key) {
    auto it = std::find_if(args.cbegin(), args.cend()
        , [key](const command::ParamView& param) {
            return param.key_ == key;
        });
    return (it == args.cend()? "": std::string{ it->value_ });
}

} // namespace {

namespace command {

    command::Args ExtractArgs(std::string_view src, char key_delimiter) {
        command::Args result;
        while (!src.empty()) {
            command::Args::value_type element;
            auto slash = src.find_first_of('-');
            if (slash == std::string_view::npos) {
                break;
            }
            auto key_end = src.find_first_of(key_delimiter, slash + 1);
            if (key_end == std::string_view::npos) break;

            element.key_ = src.substr(slash + 1, key_end - slash - 1);
            if (src.size() == key_end) {
                result.push_back(element);
                src = {};
                break;
            }
            // skip white spaces
            auto value_begin = src.find_first_not_of(' ', key_end + 1);
            if (value_begin == std::string_view::npos) {
                result.push_back(element);
                src = {};
                break;
            }
            if (src[value_begin] == '-') {
                // empty value (found new key)
                result.push_back(element);
                src.remove_prefix(value_begin);
                continue;
            }
            // value can be between \"\"
            auto delim = (src[value_begin] == '"'? '"' : ' ');
            const auto shift = delim == '"'? 1: 0;

            auto end = src.find_first_of(delim, value_begin + shift);
            if (end == std::string_view::npos) {
                // either delimiter [ ] is not found so it's end of the line
                // either no closing ["] was found -> treat rest as a value
                element.value_ = src.substr(value_begin);
                result.push_back(element);
                src = {};
            }
            else {
                auto left = value_begin + shift;
                auto count = end - left;
                element.value_ = src.substr(left, count);
                result.push_back(element);
                src.remove_prefix(end + 1);
            }
        }
        return result;
    }

    Alias Alias::Create(const service::Console&, 
        const Args& args
    ) {
        auto alias { ::Find(args, "alias") };
        auto command { ::Find(args, "command") };
        std::vector<ParamData> params;
        params.reserve(args.size());
        for (auto&& [k, v]: args) {
            params.push_back(command::ParamData{ 
                std::string(k), std::string(v) });
        }
        return { std::move(alias), std::move(command), std::move(params) };
    }

    RealmStatus RealmStatus::Create(const service::Blizzard&,
        const Args& args
    ) {
        auto channel { ::Find(args, "channel") };
        auto user { ::Find(args, "user") };

        return { std::move(channel), std::move(user) };
    }

    RealmStatus RealmStatus::Create(const service::Twitch&,
        const Args& args
    ) {
        auto channel { ::Find(args, "channel") };
        auto user { ::Find(args, "user") };
        return { std::move(channel), std::move(user) };
    }

    Arena Arena::Create(const service::Blizzard&, const Args& args) {
        auto channel { ::Find(args, "channel") };
        auto user { ::Find(args, "user") };
        auto player { ::Find(args, "player") };
        return { std::move(channel), std::move(user), std::move(player) };
    }

    Arena Arena::Create(const service::Twitch&, const Args& args) {
        auto channel { ::Find(args, "channel") };
        auto user { ::Find(args, "user") };
        auto player { ::Find(args, "player") };
        return { std::move(channel), std::move(user), std::move(player) };
    }

    Login Login::Create(const service::Twitch& ctx, const Args& args) {
        auto user { ::Find(args, "user") };
        auto token { ::Find(args, "token") };

        if (user.empty() && token.empty()) {
            auto secret = ctx.GetConfig()->GetSecret("twitch");
            return { std::move(secret->user_), std::move(secret->token_) };
        }
        return { std::move(user), std::move(token) };
    }

    Join Join::Create(const service::Twitch&, const Args& args) {
        return { ::Find(args, "channel") };
    }

    Leave Leave::Create(const service::Twitch&, const Args& args) {
        return { ::Find(args, "channel") };
    }

    Chat Chat::Create(const service::Twitch&, const Args& args) {
        auto channel { ::Find(args, "channel") };
        auto message { ::Find(args, "message") };
        return { std::move(channel), std::move(message) };
    }

}