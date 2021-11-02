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
    assert(shard_);
    shard_->Reset();
    work_.reset();
    // don't stop context to let it finish all jobs 
    // and shutdown connections gracefully
}

void Twitch::Run() {
    for (size_t i = 0; i < kThreads; i++) {
        /**
         * Source: https://en.cppreference.com/w/cpp/memory/shared_ptr
         * All member functions (including copy constructor and copy assignment) 
         * can be called by multiple threads on different instances of shared_ptr 
         * without additional synchronization even if these instances are copies 
         * and share ownership of the same object.
         * 
         * So, make copy of shared_ptr for each thread
        */
        threads_.emplace_back([ctx = this->context_] {
            for (;;) {
                try {
                    // Should be thread safe because `ctx` is another instance of shared_ptr and 
                    // it's thread safe to call `io_context::run()`
                    ctx->run();
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