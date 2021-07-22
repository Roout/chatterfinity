#pragma once

#include <vector>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <optional>

class Translator {
public:
    using Params = std::vector<std::string_view>;
    using Handle = std::function<void(const Params&)>;
    using Pair   = std::pair<const std::string_view, Handle>;

    Translator() = default;

    void Insert(const Pair& commandWithHandle) {
        table_.insert(commandWithHandle);
    }

    void Insert(std::initializer_list<Pair> commandList) {
        table_.insert(commandList);
    }

    std::optional<Handle> GetHandle(std::string_view command) const noexcept {
        if (auto it = table_.find(command); it != table_.end()) {
            return std::make_optional<Handle>(it->second);
        }
        return std::nullopt;
    }

    template<typename Command, typename Service>
    static Handle CreateHandle(Service& ctx) noexcept {
        return [&ctx](const Params& params) mutable {
            Execute(Command::Create(ctx, params), ctx);
        };
    }

private:
    // maps command to appropriate handle
    std::unordered_map<std::string_view, Handle> table_;
};