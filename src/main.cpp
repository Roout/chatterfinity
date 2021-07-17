#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Command.hpp"
#include "Translator.hpp"
#include "Console.hpp"
#include "Blizzard.hpp"
#include "Config.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

class App {
public:
    App() 
        : commands_ { kSentinel }
        , config_ { kConfigPath }
        , blizzard_ { std::make_shared<Blizzard>(&config_) }
        , console_ { &commands_ }
    {
        using namespace std::literals::string_view_literals;

        std::initializer_list<Translator::Pair> list {
            {"realm-id"sv,      Translator::CreateHandle<command::RealmID>(*blizzard_) },
            {"realm-status"sv,  Translator::CreateHandle<command::RealmStatus>(*blizzard_) },
            {"token"sv,         Translator::CreateHandle<command::AccessToken>(*blizzard_) }
        };
        translator_.Insert(list);

        config_.Read();
    }

    ~App() {
        blizzard_->ResetWork();
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
                        Console::Write(std::this_thread::get_id(), ":  -> queue is empty\n");
                        break;
                    }
                    if (auto handle = translator_.GetHandle(cmd->command_); handle) {
                        std::invoke(*handle, std::vector<std::string_view> { 
                            cmd->params_.begin(), 
                            cmd->params_.end() 
                        });
                    }
                    else {
                        Console::Write("Can not recognize a command:", cmd->command_);
                    }
                }
            });
        }
        // run services:

        // -> doesn't block
        blizzard_->Run();
        // -> blocks
        console_.Run();
    }

private:
    static constexpr bool kSentinel { true };
    static constexpr std::size_t kWorkerCount { 2 };
    static constexpr char * const kConfigPath { "secret/secrets.json" };

    std::vector<std::thread> workers_;
    // common queue
    CcQueue<command::RawCommand> commands_;
    Translator translator_;
    // configuration and settings
    Config config_;
    // services:
    std::shared_ptr<Blizzard> blizzard_;
    Console console_;
};

int main() {
    App app;
    app.Run();
    return 0;
}