#include "Console.hpp"
#include "Utility.hpp"

namespace service {

Console::Console(Container * inbox) 
    : inbox_ { inbox }
    , translator_ {}
    , invoker_ { std::make_unique<Invoker>(this) }
{
    assert(inbox_ != nullptr);

    using namespace std::literals::string_view_literals;

    std::initializer_list<Translator::Pair> list {
        { "shutdown"sv, Translator::CreateHandle<command::Shutdown>(*this) },
        { "help"sv,     Translator::CreateHandle<command::Help>(*this) }
    };
    translator_.Insert(list);
}

Console::~Console() {
    Write("  -> close console service\n");
}

std::string Console::ReadLine() {
    std::string buffer;
    std::lock_guard<std::mutex> lock { in_ };
    std::getline(std::cin, buffer);
    return buffer;
}

void Console::ReadLine(std::string& buffer) {
    std::lock_guard<std::mutex> lock { in_ };
    std::getline(std::cin, buffer);
}

// blocks execution thread
void Console::Run() {
    using namespace std::literals;

    static constexpr std::string_view kDelimiter { " " };
    std::string buffer;
    while (running_) {
        ReadLine(buffer);
        std::string_view input { buffer };
        input = utils::Trim(input);
        // parse buffer
        std::vector<std::string_view> args;
        auto delimiter = input.find(kDelimiter);
        while (delimiter != std::string_view::npos) {
            bool isQuoted = false;
            if (input.front() == '"') {
                if (auto end = input.find('"', 1); // ignore first character which may be ["]
                    end != std::string_view::npos)
                {
                    // TODO: handle messages without space after ["]: "some long message" no space after; 
                    delimiter = end;
                    isQuoted = true;
                }
            }
            const size_t ignored = isQuoted? 1: 0;
            args.emplace_back(input.data() + ignored, delimiter - ignored);
            // remove prefix with delimiter
            input.remove_prefix(delimiter + 1);
            // remove all special characters from the input
            input = utils::Trim(input);
            // update delimiter position
            delimiter = input.find(kDelimiter);
        }
        if (!input.empty()) {
            args.emplace_back(input);
        }
        
        assert(!args.empty() && "Args list can't be empty");

        if (!args.front().empty()) {
            args.front().remove_prefix(1);
        }

        if (auto handle = translator_.GetHandle(args.front()); handle) {
            Write("[console] call handle for:", args.front(), '\n');
            // proccess command here
            std::invoke(*handle, Translator::Params{++args.begin(), args.end()});
        }
        else {
            // can not recognize the command, pass it to other services
            command::RawCommand raw  { 
                std::string { args.front() }, 
                std::vector<std::string>{ ++args.begin(), args.end() }
            };
            if (!inbox_->TryPush(std::move(raw))) {
                // abandon the command
                Write("[console] fail to proccess command: command storage is full\n");
            }
        }

        std::string merged;
        for (auto&&arg: args) merged += std::string(arg) + " "; 
        Write("[console] parsed: [ ", merged, "]\n");
    }
}

void Console::Invoker::Execute(command::Shutdown) {
    assert(console_->inbox_ != nullptr && "Queue can not be NULL");
    console_->inbox_->DisableSentinel();
    console_->running_ = false;
}

void Console::Invoker::Execute(command::Help) {
    Console::Write("[console] available commands:\n"
        "  !shutdown - exit the application\n"
        "  !help - show existing commands\n"
        "  !blizzard-token - acquire token fromn blizzard\n"
        "  !realm-id - get id of the [flamegor] realm\n"
        "  !realm-status - get status of the [flamegor] realm\n"
        "  !validate - validate token for twitch\n"
        "  !login - login twitch\n"
        "  !join <channel> - join channel\n"
        "  !chat <channel> \"<message>\" - send a message to chat of the specified channel\n"
        "  !leave <channel> - leave joined channel\n"
    );
}

} // namespace service