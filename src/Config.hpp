#pragma once

#include <string>
#include <optional>
#include <unordered_map>

class Config final {
public:
    using Identity = std::string;
    struct Secret {
        std::string id_;
        std::string value_;
    };

    Config(std::string path);
    
    void Read();

    std::optional<Secret> GetSecret(const Identity& identity) const;

private:
    const std::string path_;

    std::unordered_map<Identity, Secret> services_;
};