#include "Twitch.hpp"
#include "Console.hpp"
#include "Connection.hpp"
#include "Request.hpp"
#include "Command.hpp"

#include "rapidjson/document.h"

namespace service {

Twitch::Twitch(const Config *config) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , irc_ { std::make_shared<IrcConnection>(context_, ssl_, 0) }
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
    const char * const kVerifyFilePath = "crt/StarfieldServicesRootCA.crt.pem";
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
    irc_->Close();
    irc_.reset();
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

void Twitch::Invoker::Execute(command::Help) {
    assert(false && "TODO: not implemented");
}

void Twitch::Invoker::Execute(command::Pong) {
    auto pongRequest = twitch::Pong{}.Build();
    assert(false && "TODO: not implemented");
}

void Twitch::Invoker::Execute(command::Validate) {
    assert(false && "TODO: not implemented");
}

void Twitch::Invoker::Execute(command::Shutdown) {
    assert(false && "TODO: not implemented");    
}

void Twitch::Invoker::Execute(command::Login cmd) {
    auto connectRequest = twitch::IrcAuth{cmd.token_, cmd.user_}.Build();
    assert(twitch_ && twitch_->irc_ && "Cannot be null");
    
    auto onConnect = [request = std::move(connectRequest)
        , irc = twitch_->irc_.get()
    ]() {
        irc->Write(request, [irc]() {
            // TODO: must always read!
            irc->Read([irc]() {
                auto response = irc->AcquireResponse();
                std::string raw = response.prefix_ + ":" + response.command_ + ":";
                for(auto& p: response.params_) raw += p + " ";
                Console::Write(raw, '\n');
            });
        });
    };
    twitch_->irc_->Connect(twitch::kHost, twitch::kService, std::move(onConnect));
    
}



} // namespace service