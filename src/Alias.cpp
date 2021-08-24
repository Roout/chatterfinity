#include "Alias.hpp"
#include "Console.hpp"

#include <fstream>
#include <algorithm>

namespace command {

AliasTable::AliasTable() {
    Load();
}

AliasTable::~AliasTable() {
    Save();
}

void AliasTable::Add(const Bind& alias) {
    aliases_.push_back(alias);
}

void AliasTable::Add(const Alias& alias
    , const Command& cmd
    , const Params& params
) {
    aliases_.emplace_back(Bind{ alias, cmd, params });
}

std::optional<AliasTable::CommandLine> AliasTable::GetCommand(
    std::string_view alias
) {
    auto it = std::find_if(aliases_.begin(), aliases_.end()
        , [&alias](const Bind& other) {
            return other.alias == alias;
        });

    if (it != aliases_.end()) {
        return std::make_optional(CommandLine{ it->command, it->params });
    }
    return std::nullopt;
}

bool AliasTable::Remove(const Alias& alias) noexcept {
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

void AliasTable::Save() {
    std::ofstream out{ kPath.data() };
    if (!out.is_open()) {
        service::Console::Write("[alias] --warning: alias table is not saved\n");
        return;
    }

    for (auto&& [alias, cmd, params]: aliases_) {
        out << alias << " " << cmd << " " << params.size() << '\n';
        for (auto&& [key, value]: params) {
            out << key << " " << value << '\n';
        }
    }
}

void AliasTable::Load() {
    std::ifstream in{ kPath.data() };
    if (!in.is_open()) {
        service::Console::Write("[alias] --warning: alias table is not loaded\n");
        return;
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

} // namespace command