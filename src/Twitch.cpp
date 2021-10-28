#include "Twitch.hpp"
#include "Console.hpp"
#include "IrcShard.hpp"

#include <algorithm>
#include <stdexcept>

namespace service {

Twitch::Twitch(const Config *config
    , command::Queue *outbox
    , command::AliasTable *aliases
) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , config_ { config }
{
    assert(config_ && "Config is NULL");
    assert(outbox && "Queue is NULL");
    assert(aliases && "AliasTable is NULL");
    /**
     * [Amazon CA](https://www.amazontrust.com/repository/)
     * TODO: read this path from secret + with some chiper
    */
    const char * const kVerifyFilePath { "crt/StarfieldServicesRootCA.crt.pem" };
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[ERROR]: ", error.message(), '\n');
    }

    shard_ = std::make_unique<twitch::IrcShard>(
        this, outbox, aliases, context_, ssl_);
}

Twitch::~Twitch() {
    Console::Write("  -> close twitch service\n");
    for (auto& t: threads_) t.join();
}

void Twitch::ResetWork() {
    work_.reset();
    if (shard_) {
        shard_->Reset();
    }
    // don't stop context to let it finish all jobs 
    // and shutdown connections gracefully
}

void Twitch::Run() {
    for (size_t i = 0; i < kThreads; i++) {
        threads_.emplace_back([this] {
            for (;;) {
                try {
                    // TODO: clarify is there any data race around `shared_ptr`?
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

} // namespace service