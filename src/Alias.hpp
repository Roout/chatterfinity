#pragma once
#include <vector>
#include <string>
#include <string_view>
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

    AliasTable();

    AliasTable(const AliasTable&) = delete;
    AliasTable& operator= (const AliasTable&) = delete;
    AliasTable(AliasTable&&) = delete;
    AliasTable& operator= (AliasTable&&) = delete;

    ~AliasTable();

    void Add(const Bind& alias);

    void Add(const Alias& alias, const Command& cmd, const Params& params);

    std::optional<CommandLine> GetCommand(std::string_view alias) const;

    bool Remove(const Alias& alias) noexcept;

private:

    void Save();

    void Load();

    std::vector<Bind> aliases_;
    static constexpr std::string_view kPath { "alias.txt" };
    
};

} // namespace command