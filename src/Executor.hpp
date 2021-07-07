#pragma once

#include <memory>

#include <boost/asio.hpp>

/**
 * @note Lifetime of executor must be longer than `contex`
 * 
*/
struct ChainExecutor {

    ChainExecutor(std::shared_ptr<boost::asio::io_context> ctx)
        : context_ { ctx }
    {}

    template<class Head>
    void Execute(Head&& head) {
        boost::asio::post(*context_, std::forward<Head>(head));
    }

    template<class Head, class Continuation>
    void Execute(Head&& head, Continuation&& cont) {
        boost::asio::post(*context_, [ctx = context_
            , init = std::forward<Head>(head)
            , cont = std::forward<Continuation>(cont)]() 
        {
            auto res = std::invoke(init);
            boost::asio::post(*ctx, [cont = std::move(cont), arg = std::move(res)]() {
                std::invoke(cont, std::move(arg));
            });
        });
    }

    /**
     * Callable must be copiable
     */
    template<class Head, class Continuation, class ...Funcs>
    void Execute(Head&& head, Continuation&& cont, Funcs&&...fns) {
        boost::asio::post(*context_, [this
            , init = std::forward<Head>(head)
            , cont = std::forward<Continuation>(cont)
            , fns... ]() 
        {
            auto res = std::invoke(init);
            boost::asio::post(*context_, [this
                , cont = std::move(cont)
                , arg = std::move(res)
                , fns...]() mutable 
            {
                auto func = std::bind(cont, std::move(arg));
                Execute(std::move(func), std::move(fns)...);
            });
        });
    }

    std::shared_ptr<boost::asio::io_context> GetContext() const {
        return context_;
    }

private:
    std::shared_ptr<boost::asio::io_context> context_;
};
