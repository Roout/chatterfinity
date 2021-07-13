#include "Console.hpp"
#include "Utility.hpp"

Console::Console(CcQueue<command::RawCommand> * inbox) 
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

std::string Console::ReadLn() {
    std::string buffer;
    std::lock_guard<std::mutex> lock { in_ };
    std::getline(std::cin, buffer);
    return buffer;
}

void Console::ReadLn(std::string& buffer) {
    std::lock_guard<std::mutex> lock { in_ };
    std::getline(std::cin, buffer);
}

// blocks execution thread
void Console::Run() {
    using namespace std::literals;

    static constexpr std::string_view kDelimiter { " " };
    std::string buffer;
    while (running_) {
        ReadLn(buffer);
        std::string_view input { buffer };
        input = utils::Trim(input);
        // parse buffer
        std::vector<std::string_view> args;
        auto delimiter = input.find(kDelimiter);
        while (delimiter != std::string_view::npos) {
            args.emplace_back(input.data(), delimiter);
            // remove prefix with delimiter
            input.remove_prefix(delimiter + 1);
            // remove all special characters from the input
            input = utils::Trim(input);
            // update delimiter position
            delimiter = input.find(kDelimiter);
        }
        args.emplace_back(input);
        
        assert(!args.empty() && "Args list can't be empty");

        if (!args.front().empty()) {
            args.front().remove_prefix(1);
        }

        if (auto handle = translator_.GetHandle(args.front()); handle) {
            Write("Call handle:", args.front(), '\n');
            // try to proccess command here
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
                Write("fail to proccess command: command storage is full\n");
            }
        }

        Write("  -> parsed: [ "sv, args.front(), ", ... ]\n"sv);
    }
}

class Invoker {
public:
    Invoker(Console *console) : console_ { console } {}

    void Execute(command::Shutdown);
    
    void Execute(command::Help);

private:
    Console * const console_ { nullptr };
};

void Invoker::Execute(command::Shutdown) {
    assert(console_->inbox_ != nullptr && "Queue can not be NULL");
    console_->inbox_->DisableSentinel();
    console_->running_ = false;
}

void Invoker::Execute(command::Help) {
    Console::Write("available commands:\n"
        "\t!shutdown - exit the application\n"
        "\t!help - show existing commands\n"
        "\t!token - acquire token fromn blizzard\n"
        "\t!realm-id - get id of the [flamegor] realm\n"
        "\t!realm-status - get status of the [flamegor] realm\n"
    );
}