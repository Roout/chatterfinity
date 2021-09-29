#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace blizzard::domain {

struct RealmStatus {
    std::string name;
    std::string queue;
    std::string status;
};

struct Token {
    std::string content;
    std::string type;
    std::uint64_t expires;
};

struct Team {
    std::string name;
    std::string realmSlug;
    std::vector<std::string> playerNames;
    int rank;
    int rating;
};

struct Arena {
    std::vector<Team> teams;
};

struct Realm {
    std::uint64_t id;
    std::string name;
    std::string queue;
    std::string status;

    Realm(std::uint64_t id)
        : id { id } {}

    Realm(std::uint64_t id
        , std::string name
        , std::string queue
        , std::string status
    ) 
        : id { id }
        , name { std::move(name) }
        , queue { std::move(queue) }
        , status { std::move(status) }
    {}    
};

enum class Domain : std::uint8_t {
    kToken,
    kArena,
    kRealm
};


bool Parse(const std::string& src, RealmStatus& dst);
bool Parse(const std::string& src, Token& dst);
bool Parse(const std::string& src, Arena& dst);
// bool Parse(const std::string& src, Realm& dst);

std::string to_string(const RealmStatus& src);
std::string to_string(const Token& src);
std::string to_string(const Team& src);
std::string to_string(const Arena& src);
std::string to_string(const Realm& src);

} // namespace blizzard::domain