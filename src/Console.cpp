#include "Console.hpp"
#include "Utility.hpp"
#include "Environment.hpp"

#include <sstream>
#include <algorithm>
#include <cctype> // std::tolower

#include <cassert>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

#ifdef _WIN32

void PrintError() {
    const DWORD id = ::GetLastError();
    LPTSTR buffer = nullptr;
    const auto kMask { FORMAT_MESSAGE_ALLOCATE_BUFFER 
        | FORMAT_MESSAGE_FROM_SYSTEM 
        | FORMAT_MESSAGE_IGNORE_INSERTS };
    if (!FormatMessage(kMask, nullptr, id
        , MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
        , (LPTSTR)&buffer, 0, nullptr)) 
    {
        service::Console::Write("[console]: --error \"FormatMessage "
            "call failed\" with error id:", id, "\n");
    }
    else {
        service::Console::Write("[console]: --error", buffer, "\n");
    }
    LocalFree(buffer);
}

#endif

} // namespace {

namespace service {

Console::Console(Container * inbox, command::AliasTable * aliases) 
    : inbox_ { inbox }
    , translator_ {}
    , aliases_ { aliases }
    , invoker_ { std::make_unique<Invoker>(this) }
{
    assert(inbox_ != nullptr);
    assert(aliases_ != nullptr);

    using namespace std::literals::string_view_literals;

    std::initializer_list<Translator::Pair> list {
        { "shutdown"sv, Translator::CreateHandle<command::Shutdown>(*this) }
        , { "help"sv,     Translator::CreateHandle<command::Help>(*this) }
        , { "alias"sv,    Translator::CreateHandle<command::Alias>(*this) }
    };
    translator_.Insert(list);
}

Console::~Console() {
    Write("  -> close console service\n");
}

#ifdef _WIN32

std::string Console::ReadLine() {
    void *handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        PrintError();
        return {};
    }

    constexpr size_t kMaxChars = cst::kMaxIrcMessageSize;
    // should have 3 bytes at most for one character
    constexpr size_t kMaxBytes = kMaxChars * 3 + 1;
    
    // The number of characters to be read. 
    // The size of the buffer pointed to by the lpBuffer parameter 
    // should be at least nNumberOfCharsToRead * sizeof(TCHAR) bytes.
    wchar_t wides[kMaxChars * sizeof(wchar_t)];
    char alias[kMaxBytes];
    // number of characters will be read
    unsigned long read = 0; 
    
    std::unique_lock<std::mutex> guard { in_ };
    // TODO: add mechanism to catch the case 
    // when user entered more than `kMaxChars` characters
    const auto code = ReadConsoleW(handle, wides, kMaxChars, &read, nullptr);
    guard.unlock();
    if (!code) {
        PrintError();
        return {};
    }

    // remove CRLF delimiter from the string
    assert(read >= 2 && "Assume, that delimiter is CRLF");
    read -= 2;
    wides[read] = L'\0';

    if (auto size = WideCharToMultiByte(CP_UTF8, 0, wides, read
        , alias, kMaxBytes, nullptr, nullptr); size > 0) 
    { // success
        alias[size] = '\0';
        return { alias, static_cast<size_t>(size) };
    }
    else {
        PrintError();
        return {};
    }
}

void Console::ReadLine(std::string& buffer) {
    buffer = ReadLine();
}

#elif (__unix__ || __linux__)

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

#endif

void Console::Run() {
    using namespace std::literals;

    std::string buffer;
    while (running_) {
        ReadLine(buffer);
        auto input { utils::Trim(buffer) };
        if (input.empty()) continue;
        // parse buffer
        auto sign = input.front();
        input.remove_prefix(1);

        std::string_view cmd;
        if (auto space = input.find_first_of(' '); 
            space == std::string_view::npos
        ) {
            cmd = input;
        }
        else {
            cmd = input.substr(0, space);
            input.remove_prefix(space + 1);
        }

        command::Args params;
        if (sign == '!') {
            params = command::ExtractArgs(input, ' ');
        }

        // TODO: remove code duplication: Twitch.cpp / Console.cpp
        assert(aliases_);
        // Substitute alias with command and required params if it's alias
        auto referred = aliases_->GetCommand(cmd);
        if (referred) {
            Write("[console] used alias ", cmd
                , "refers to ", referred->command, '\n');
            cmd = referred->command;
            for (const auto& [k, v]: referred->params) {
                const auto& key = k;
                if (auto it = std::find_if(params.cbegin(), params.cend(), 
                    [&key](const command::ParamView& data) {
                        return data.key_ == key; 
                    }); it == params.cend()
                ) { // add param to param list if it's not already provided
                    params.push_back(command::ParamView{ 
                        std::string_view{ k }, std::string_view{ v } });
                }
            }
        }       
        Dispatch(cmd, params);
       
        std::stringstream ss;
        ss << cmd << ' ';
        for (auto&& [k, v]: params) ss << k << " " << v << ' '; 
        Write("[console] parsed: [ ", ss.str(), "]\n");
    }
}

void Console::Dispatch(std::string_view cmd, const command::Args& args) {
    std::string lowerCaseCmd{ cmd };
    std::transform(lowerCaseCmd.cbegin(), lowerCaseCmd.cend()
        , lowerCaseCmd.begin(), [](char c) { return std::tolower(c); });

    if (auto handle = translator_.GetHandle(lowerCaseCmd); handle) {
        Write("[console] call handle for:", lowerCaseCmd, '\n');
        // proccess command here
        std::invoke(*handle, args);
    }
    else {
        // can not recognize the command, pass it to other services
        std::vector<command::ParamData> params;
        params.reserve(args.size());
        for (auto&& [k, v]: args) {
            params.push_back(command::ParamData{ 
                std::string(k), std::string(v) });
        }        
        command::RawCommand raw { std::move(lowerCaseCmd), std::move(params) };
        if (!inbox_->TryPush(std::move(raw))) {
            // abandon the command
            Write("[console] fail to proccess command:"
                " command storage is full\n");
        }
    }
}

void Console::Invoker::Execute(command::Alias cmd) {
    assert(console_->aliases_ != nullptr);
    if (auto handle = console_->translator_.GetHandle(cmd.alias_); !handle) {
        console_->aliases_->Add(cmd.alias_, cmd.command_, cmd.params_);
    }
    else {
        Write("[console] alias", cmd.alias_
            , "can't be created: it coincidened with existing command!");
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
        "  !join -channel <channel_name> - join channel\n"
        "  !chat -channel <channel_name> -message \"<message>\""
            " - send a message to chat of the specified channel\n"
        "  !leave -channel <channel_name> - leave joined channel\n"
    );
}

} // namespace service