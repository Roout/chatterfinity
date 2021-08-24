#include "Chain.hpp"
#include <cassert>

Chain::Chain(std::shared_ptr<boost::asio::io_context> context)
    : context_{ context }
{
    assert(context_);
}

Chain& Chain::Add(Chain::Callback cb) {
    assert(cb); 
    return Add([](Chain::Callback arg) { std::invoke(arg); }
        , std::move(cb)
    );
}

Chain& Chain::Add(Chain::Task task, Chain::Callback cb) {
    // `hook` will be executed in `task` instead of original callback `cb`
    auto hook = [self = shared_from_this(), cb = std::move(cb)]() {
        if (cb) cb();
        if (!self->chain_.empty()) self->Execute();
    };
    chain_.emplace_back(std::move(task), std::move(hook));
    return *this;
}

void Chain::Execute() {
    assert(!chain_.empty());

    boost::asio::post(*context_, [self = shared_from_this()]() {
        assert(!self->chain_.empty());

        auto ctx { std::move(self->chain_.front()) };
        self->chain_.pop_front();
        std::invoke(ctx.task, std::move(ctx.cb)); 
    });
}