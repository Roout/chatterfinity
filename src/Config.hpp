#pragma once

#include <string>
#include <optional>
#include <unordered_map>

class Config final {
public:
    using Identity = std::string;

    struct Secret {
        std::string id_;
        std::string user_;
        std::string token_;
        std::string secret_;
    };

    Config(std::string path);
    
    void Read();

    std::optional<Config::Secret> GetSecret(const Identity& service) const;

private:
    const std::string path_;

    std::unordered_map<Identity, Secret> services_;
};