#pragma once

#include <string>
#include <fstream>
#include <cassert>

#include "Utility.hpp"

struct Config {
    std::string id_;
    std::string secret_;

    static constexpr std::string_view kConfigPath = "secret/secrets.cfg";
    static constexpr char kDelimiter = ':';

public:
    void Read() {
        std::ifstream in(kConfigPath.data());
        if (!in.is_open()) {
            throw std::exception("Failed to open config file");
        }
        std::string buffer;
        while (getline(in, buffer)) {
            const auto delimiter = buffer.find(kDelimiter);
            if (delimiter == std::string::npos) {
                throw std::exception("Wrong config file format");
            }

            std::string_view key { buffer.data(), delimiter };
            key = utils::Trim(key, " \"\n");
            std::string_view value { buffer.data() + delimiter + 1 };
            value = utils::Trim(value, " \"\n");
            
            if (utils::IsEqual(key, "id")) {
                id_.assign(value.data(), value.size());
            }
            else if (utils::IsEqual(key, "secret")) {
                secret_.assign(value.data(), value.size());
            }
            else {
                assert(false && "Unreachable: unexcpected key");
            }
        }
        assert(!id_.empty());
        assert(!secret_.empty());
    }

};