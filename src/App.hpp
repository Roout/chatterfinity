#pragma once

#include <string>
#include <string_view>
#include <vector>
// 3rd party
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
// common:
#include "Command.hpp"
#include "ConcurrentQueue.hpp"
#include "Translator.hpp"
#include "Config.hpp"
#include "Alias.hpp"
#include "Environment.hpp"
// services:
#include "Console.hpp"
#include "Blizzard.hpp"
#include "Twitch.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class App {
public:
    using Container = CcQueue<command::RawCommand, cst::kQueueCapacity>;

    App() 
        : commands_ { kSentinel }
        , config_ { kConfigPath }
        , aliases_ {}
        , blizzard_ { std::make_shared<service::Blizzard>(&config_, &commands_) }
        , twitch_ { std::make_shared<service::Twitch>(&config_, &commands_) }
        , console_ { &commands_, &aliases_}
    {
        config_.Read();

        using namespace std::literals::string_view_literals;
        std::initializer_list<Translator::Pair> list {
            {"realm-id"sv,      Translator::CreateHandle<command::RealmID>(*blizzard_) },
            {"realm-status"sv,  Translator::CreateHandle<command::RealmStatus>(*blizzard_) },
            {"blizzard-token"sv,Translator::CreateHandle<command::AccessToken>(*blizzard_) },
            {"arena"sv,         Translator::CreateHandle<command::Arena>(*blizzard_) },
            
            {"validate"sv,      Translator::CreateHandle<command::Validate>(*twitch_) },
            {"login"sv,         Translator::CreateHandle<command::Login>(*twitch_) },
            {"join"sv,          Translator::CreateHandle<command::Join>(*twitch_) },
            {"chat"sv,          Translator::CreateHandle<command::Chat>(*twitch_) },
            {"leave"sv,         Translator::CreateHandle<command::Leave>(*twitch_) }
        };
        translator_.Insert(list);
    }

    App(const App&) = delete;
    App& operator= (const App&) = delete;
    App(App&&) = delete;
    App& operator= (App&&) = delete;

    ~App() {
        blizzard_->ResetWork();
        twitch_->ResetWork();
        for (auto&&worker: workers_) {
            worker.join();
        }
    }

    void Run() {
        // create consumers
        for (std::size_t i = 0; i < kWorkerCount; i++) {
            workers_.emplace_back([this]() {
                while (true) {
                    auto cmd { commands_.TryPop() };
                    if (!cmd) {
                        service::Console::Write(std::this_thread::get_id(), ":  -> queue is empty\n");
                        break;
                    }
                    if (auto handle = translator_.GetHandle(cmd->command_); handle) {
                        Translator::Params params;
                        params.reserve(cmd->params_.size());
                        for (auto&& [k, v]: cmd->params_) {
                            params.emplace_back(command::ParamView{ k, v });
                        }        
                        std::invoke(*handle, params);
                    }
                    else {
                        service::Console::Write("Can not recognize a command:", cmd->command_, '\n');
                    }
                }
            });
        }
        // run services:

        // -> doesn't block
        blizzard_->Run();
        twitch_->Run();
        // -> blocks
        console_.Run();
    }

private:
    // enable/disable sentinel in CcQueue 
    static constexpr bool kSentinel { true };
    static constexpr std::size_t kWorkerCount { 2 };
    static constexpr const char * const kConfigPath { "secret/services.json" };

    std::vector<std::thread> workers_;
    // common queue
    Container commands_;
    Translator translator_;
    // configuration and settings
    Config config_;
    command::AliasTable aliases_;
    // services:
    std::shared_ptr<service::Blizzard> blizzard_;
    std::shared_ptr<service::Twitch> twitch_;
    service::Console console_;
};
