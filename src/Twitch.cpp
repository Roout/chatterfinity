#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"

#include "rapidjson/document.h"

namespace service {

Twitch::Twitch(const Config *config) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
{
    assert(config_ && "Config is NULL");
    /**
     * [Amazon CA](https://www.amazontrust.com/repository/)
     * Distinguished Name:
     * ```
     * CN=Starfield Services Root Certificate Authority - G2,
     * O=Starfield Technologies\, Inc.,
     * L=Scottsdale,
     * ST=Arizona,
     * C=US
     * ```
     * TODO: read this path from secret + with some chiper
    */
    const char * const kVerifyFilePath = "StarfieldServicesRootCA.crt.pem";
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[ERROR]: ", error.message(), '\n');
    }
}

Twitch::~Twitch() {
    Console::Write("  -> close twitch service\n");
    for (auto& t: threads_) t.join();
}

void Twitch::ResetWork() {
    work_.reset();
}

void Twitch::Run() {
    for (size_t i = 0; i < kThreads; i++) {
        threads_.emplace_back([this](){
            for (;;) {
                try {
                    context_->run();
                    break;
                }
                catch (std::exception& ex) {
                    // some system exception
                    Console::Write("[ERROR]: ", ex.what(), '\n');
                }
            }
        });
    }
}

void Twitch::Login(std::function<void()> continuation) {

}

void Twitch::Invoker::Execute(command::Help) {

}

void Twitch::Invoker::Execute(command::Validate) {

}

void Twitch::Invoker::Execute(command::Shutdown) {

}

void Twitch::Invoker::Execute(command::Login cmd) {
    auto initiateQuery = [twitch = twitch_]() {
        assert(false && "TODO: ");
    };
    std::invoke(initiateQuery);
}



} // namespace service