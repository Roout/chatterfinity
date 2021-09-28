#include "Domain.hpp"

#include <type_traits>
#include <cassert>
#include <sstream>
#include <iomanip> // std::quoted

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

namespace {

template<typename T>
struct is_extractable_value {
    static constexpr bool value { 
        std::is_same_v<T, bool>
        || std::is_same_v<T, std::uint64_t>
        || std::is_same_v<T, int>
        || std::is_same_v<T, std::string>
    };
};

// utility function
// works for: bool, string, int, uint64_t
template<typename T, 
    typename = std::enable_if_t<is_extractable_value<T>::value>
>
bool Copy(const rapidjson::Value& src, T& dst, const char *key) {
    if (auto it = src.FindMember(key); it != src.MemberEnd()) {
        if constexpr (std::is_same_v<bool, T>) {
            dst = it->value.GetBool();
            return true;
        }
        else if constexpr (std::is_same_v<std::uint64_t, T>) {
            dst = it->value.GetUint64();
            return true;
        }
        else if constexpr (std::is_same_v<int, T>) {
            dst = it->value.GetInt();
            return true;
        }
        else if constexpr (std::is_same_v<std::string, T>) {
            dst = it->value.GetString();
            return true;
        }
    }
    return false;
}

} // namespace {

namespace blizzard::domain {

bool Parse(const std::string& src, RealmStatus& dst) {
    // I wish to avoid partially initialized entities
    // so I'm trying to read entity from json
    RealmStatus parsed;

    rapidjson::Document json; 
    json.Parse(src.data(), src.size());
    if (!json.HasMember("realms")) { 
        return false;
    }
    const auto realms = json["realms"].GetArray();
    assert(!realms.Empty() && "Empty realms");

    const auto& front = *realms.Begin();
    if (!front.HasMember("name")) { 
        return false;
    }
    parsed.name = front["name"].GetString();

    if (!json.HasMember("has_queue")) { 
        return false;
    }
    const auto hasQueue = json["has_queue"].GetBool();
    parsed.queue = hasQueue? "has queue": "no queue";

    if (!json.HasMember("status") || !json["status"].HasMember("type")) { 
        return false;
    }
    parsed.status = json["status"]["type"].GetString();

    dst = std::move(parsed);
    return true;
}

bool Parse(const std::string& src, Token& dst) {
    Token parsed;
    rapidjson::Document json; 
    json.Parse(src.data(), src.size());

    if (!::Copy(json, parsed.content, "access_token")) {
        return false;
    }
    if (!::Copy(json, parsed.type, "token_type")) {
        return false;
    }
    if (!::Copy(json, parsed.expires, "expires_in")) {
        return false;
    }

    assert(parsed.type == "bearer" 
        && "Unexpected token type. Blizzard API may be changed!");


    dst = std::move(parsed);
    return true;
}

// assisting method
bool Parse(const rapidjson::Value& teamValue, Team& dst) {
    Team parsed;
    if (!::Copy(teamValue, parsed.name, "name")) {
        return false;
    }
    // realm
    auto realm = teamValue.FindMember("realm");
    if (realm == teamValue.MemberEnd()) {
        return false;
    }
    if (!::Copy(realm->value, parsed.realmSlug, "slug")) {
        return false;
    }
    // members
    auto membersIt = teamValue.FindMember("members");
    if (membersIt == teamValue.MemberEnd()) {
        // parsed has no members
        return true;
    }
    const auto& members = membersIt->value.GetArray();
    
    parsed.playerNames.resize(members.Size());
    size_t i = 0;
    for (auto&& player: members) {
        auto character = player.FindMember("character");
        if (character == player.MemberEnd()) {
            return false;
        }
        if (!::Copy(character->value, parsed.playerNames[i], "name")) {
            return false;
        }
        ++i;
    }

    dst = std::move(parsed);
    return true;
}

bool Parse(const std::string& src, Arena& dst) {
    Arena parsed;

    rapidjson::Document json; 
    json.Parse(src.data(), src.size());

    auto entriesIt = json.FindMember("entries");
    if (entriesIt == json.MemberEnd()) {
        return false;
    }
    const auto& entries = entriesIt->value.GetArray();
    for (auto&& entry: entries) {
        Team team;
        if (!::Copy(entry, team.rank, "rank")) {
            return false;
        }
        if (!::Copy(entry, team.rating, "rating")) {
            return false;
        }

        auto teamIt = entry.FindMember("team");
        if (teamIt == entry.MemberEnd()) {
            return false;
        }
        const auto& teamValue = teamIt->value;
        if (!Parse(teamValue, team)) {
            return false;
        }
        parsed.teams.emplace_back(std::move(team));
    }
    dst = std::move(parsed);
    return true;
}

std::string to_string(const RealmStatus& realm) {
    return realm.name + "(" + realm.status + "): " + realm.queue;
}

std::string to_string(const Token& token) {
    std::stringstream ss;
    ss << "Token: " << token.type << " : " << token.content 
        << " (" << token.expires <<  ")";
    return ss.str();
}

std::string to_string(const Team& team) {
    std::stringstream ss;
    ss << std::quoted(team.name, '\'') << " " << team.realmSlug << " " 
        << std::to_string(team.rank) << " " << std::to_string(team.rating) << " "
        << std::to_string(team.playerNames.size()) << ":";
    for (const auto& player: team.playerNames) {
        ss << " " << std::quoted(player, '\'');
    }
    return ss.str();
}

std::string to_string(const Arena& src) {
    assert(false && "TODO: not implemented");
    return {};
}

std::string to_string(const Realm& src) {
    assert(false && "TODO: not implemented");
    return {};
}

} // namespace blizzard::domain