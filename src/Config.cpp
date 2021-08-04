#include "Config.hpp"

#include <cassert>
#include <stdexcept>
#include <array>
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
        throw std::runtime_error("Failed to open config file");
    }

    const std::string buffer {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };

    auto AddMember = [](auto memberIter, std::string& dst, const char *member) {
        if (auto it = memberIter->value.FindMember(member); 
            it != memberIter->value.MemberEnd()
        ) {
            dst = it->value.GetString();
        }
    };
    rapidjson::Document doc; 
    doc.Parse(buffer.data(), buffer.size());
    for (auto serviceIter = doc.MemberBegin(); 
        serviceIter != doc.MemberEnd(); 
        ++serviceIter
    ) {
        Identity service = serviceIter->name.GetString();
        Secret secret {};

        AddMember(serviceIter, secret.id_, "client_id");
        AddMember(serviceIter, secret.user_, "user");
        AddMember(serviceIter, secret.token_, "token");
        AddMember(serviceIter, secret.secret_, "secret");

        services_.emplace(std::move(service), std::move(secret));
    }
}

std::optional<Config::Secret> Config::GetSecret(const Identity& identity) const {
    if (auto it = services_.find(identity); it != services_.end()) {
        return { it->second };
    }
    return std::nullopt;
}