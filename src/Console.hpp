#pragma once

#include <iostream>
#include <string>

#include "Command.hpp"
#include "Translator.hpp"
#include "ConcurrentQueue.hpp"

namespace service {
// This is source of input 
class Console {
public:
    Console(CcQueue<command::RawCommand> * inbox);

    Console(const Console&) = delete;
    Console(Console&&) = delete;
    Console& operator=(const Console&) = delete;
    Console& operator=(Console&&) = delete;
    
    ~Console();

    static std::string ReadLn();

    static void ReadLn(std::string& buffer);

    template<class ...Args>
    static void Write(Args&&...args);

    // blocks execution thread
    void Run();

    template<typename Command,
        typename = std::enable_if_t<command::details::is_console_api_v<Command>>
    >
    void Execute(Command&& cmd);

private:
    class Invoker;

    CcQueue<command::RawCommand> * const inbox_ { nullptr };
    Translator translator_ {};

    bool running_ { true };

    std::unique_ptr<Invoker> invoker_;

    static inline std::mutex in_ {};
    static inline std::mutex out_ {};
};


class Console::Invoker {
public:
    Console::Invoker(Console *console) : console_ { console } {}

    void Execute(command::Shutdown);
    
    void Execute(command::Help);

private:
    Console * const console_ { nullptr };
};


template<typename ...Args>
inline void Console::Write(Args&&...args) {
    std::lock_guard<std::mutex> lock { out_ };
    ((std::cout << std::forward<Args>(args) << " "), ...);
}

template<typename Command, typename Enable>
inline void Console::Execute(Command&& cmd) {
    invoker_->Execute(std::forward<Command>(cmd));
}

} // namespace service