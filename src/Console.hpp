#pragma once

#include <iostream>
#include <string>

#include "Command.hpp"
#include "Translator.hpp"
#include "ConcurrentQueue.hpp"

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

    template<class Command>
    void Execute(Command&& cmd);

private:
    CcQueue<command::RawCommand> * const inbox_ { nullptr };
    Translator translator_ {};

    bool running_ { true };

    static inline std::mutex in_ {};
    static inline std::mutex out_ {};
};

template<class ...Args>
inline void Console::Write(Args&&...args) {
    std::lock_guard<std::mutex> lock { out_ };
    ((std::cout << std::forward<Args>(args) << " "), ...);
}

template<class Command>
inline void Console::Execute(Command&& cmd) {
    if constexpr (std::is_same_v<Command, command::Shutdown>) {
        assert(inbox_ != nullptr && "Queue can not be NULL");
        inbox_->DisableSentinel();
        running_ = false;
    }
}