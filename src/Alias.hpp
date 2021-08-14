#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <optional>

#include "Command.hpp"

namespace command {

class AliasTable {
public:
    using Alias = std::string;
    using Command = std::string;
    using Params = std::vector<ParamData>;

    struct CommandLine {
        Command command;
        Params params;
    };

    struct Bind {
        Alias alias;
        Command command;
        Params params;
    };

    AliasTable() {
        Load();
    }

    AliasTable(const AliasTable&) = delete;
    AliasTable& operator= (const AliasTable&) = delete;
    AliasTable(AliasTable&&) = delete;
    AliasTable& operator= (AliasTable&&) = delete;

    ~AliasTable() {
        Save();
    }

    void Add(const Bind& alias) {
        aliases_.push_back(alias);
    }

    void Add(const Alias& alias, const Command& cmd, const Params& params) {
        aliases_.emplace_back(Bind{ alias, cmd, params });
    }

    std::optional<CommandLine> GetCommand(std::string_view alias) {
        auto it = std::find_if(aliases_.begin(), aliases_.end()
            , [&alias](const Bind& other) {
                return other.alias == alias;
            });

        if (it != aliases_.end()) {
            return std::make_optional(CommandLine{ it->command, it->params });
        }
        return std::nullopt;
    }

    bool Remove(const Alias& alias) {
        auto it = std::find_if(aliases_.begin(), aliases_.end()
            , [&alias](const Bind& other) {
                return other.alias == alias;
            });

        if (it != aliases_.end()) {
            std::swap(*it, aliases_.back());
            aliases_.pop_back();
            return true;
        }
        return false;
    }

private:

    void Save() {
        std::ofstream out{ kPath.data() };
        if (!out.is_open()) {
            // notify about error
            return; // no alias is saved
        }

        for (auto&& [alias, cmd, params]: aliases_) {
            out << alias << " " << cmd << " " << params.size() << '\n';
            for (auto&& [key, value]: params) {
                out << key << " " << value << '\n';
            }
        }
    }

    void Load() {
        std::ifstream in{ kPath.data() };
        if (!in.is_open()) {
            // notify about error
            return; // no alias loaded
        }

        Alias alias;
        Command cmd;
        size_t params_count;
        while (in >> alias >> cmd >> params_count) {
            std::vector<ParamData> params;
            params.reserve(params_count);
            ParamData data;
            for (size_t i = 0; i < params_count; i++) {
                in >> data.key_ >> data.value_;
                params.push_back(std::move(data));
            }
            aliases_.push_back(Bind{ 
                std::move(alias), std::move(cmd), std::move(params)
            });
        }
    }

    std::vector<Bind> aliases_;
    static constexpr std::string_view kPath { "alias.txt" };
    
};

} // namespace command