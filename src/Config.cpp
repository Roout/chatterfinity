#include "Config.hpp"

#include <cassert>
#include <fstream>
#include <streambuf>

#include "rapidjson/document.h"

Config::Config(std::string path)
    : path_ { std::move(path) }
{
    assert(!path_.empty());
}

void Config::Read() {
    std::ifstream in(path_);
    if (!in.is_open()) {
        throw std::exception("Failed to open config file");
    }

    const std::string buffer {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };

    rapidjson::Document reader; 
    reader.Parse(buffer.data(), buffer.size());
    const auto& services = reader["services"];
    for (auto&& service: services.GetArray()) {
        assert(service.HasMember("identity") && "Config: Absent service identity");
        Identity identity { service["identity"].GetString() };
        
        assert(service.HasMember("id") && "Config: Absent service id");
        assert(service.HasMember("secret") && "Config: Absent service secret");
        Secret secret { service["id"].GetString(), service["secret"].GetString() };
        
        assert(services_.find(identity) == services_.end() && "service already exist");
        services_.emplace(std::move(identity), std::move(secret));
    }
}

std::optional<Config::Secret> Config::GetSecret(const Identity& identity) const {
    if (auto it = services_.find(identity); it != services_.end()) {
        return { it->second };
    }
    return std::nullopt;
}