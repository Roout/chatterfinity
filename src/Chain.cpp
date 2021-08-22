#include "Chain.hpp"
#include <cassert>

Chain& Chain::Add(Chain::Callback cb) {
    assert(cb); 
    return Add(
        [](Chain::Callback arg) { std::invoke(arg); }
        , std::move(cb)
    );
}

Chain& Chain::Add(Chain::Task task, Chain::Callback cb) {
    // `hook` will be executed in `task` instead of original callback `cb`
    auto hook = [self = shared_from_this(), cb = std::move(cb)](){
        if (cb) cb();
        self->Execute();
    };
    chain_.emplace_back(std::move(task), std::move(hook));

    if (current_ == chain_.end()) {
        current_ = chain_.begin();
    }
    return *this;
}

void Chain::Execute() {
    if (current_ == chain_.end()) {
        // all tasks are completed
        return;
    }

    auto prev = current_++;
    try {
        std::invoke(prev->task, std::move(prev->cb));
    }
    catch (...) {
        --current_;
        throw;
    }
}