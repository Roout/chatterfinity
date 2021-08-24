#pragma once
#include <list>
#include <memory>
#include <functional>

#include <boost/asio.hpp>

/**
 * Chain MUST NOT (and IS NOT) observed by more than 1 thread at once
 * TODO: remove already completed tasks from the chain
 */
class Chain : public std::enable_shared_from_this<Chain> {
public:
    using Callback = std::function<void()>;
    using Task = std::function<void(Callback)>;

    Chain(std::shared_ptr<boost::asio::io_context> context);

    Chain& Add(Callback cb);

    Chain& Add(Task task, Callback cb = {});

    void Execute();

private:

    struct Bind {
        Bind(Task task, Callback cb)
            : task { std::move(task) }
            , cb { std::move(cb) }
        {}

        Task task;
        // callback is being passed to task as parameter via std::move
        // and will be destroyed with task. 
        Callback cb;
    };

    std::shared_ptr<boost::asio::io_context> context_;
    std::list<Bind> chain_;
};
