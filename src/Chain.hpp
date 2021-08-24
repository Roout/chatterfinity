#pragma once
#include <list>
#include <memory>
#include <functional>

#include <boost/asio.hpp>

/**
 * It's guranteed that the tasks will be executed in queue - FIFO.
 * The next task can not start executing earlier 
 * than the current's task callback is called.
 * 
 * Chain stores all tasks + callbacks
 * Chain is not observed by more than one thread at once
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
