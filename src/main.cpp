#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <exception>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <iterator>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "Response.hpp"
#include "Request.hpp"
#include "Utility.hpp"
#include "Connection.hpp"
#include "Token.hpp"
#include "Config.hpp"
#include "Command.hpp"
#include "Translator.hpp"
#include "ConcurrentQueue.hpp"

namespace ssl = boost::asio::ssl;
using boost::asio::ip::tcp;

namespace temp {

    // This is source of input 
    class Console {
    public:
        Console(CcQueue<command::RawCommand> * inbox) 
            : inbox_ { inbox }
            , translator_ {}
        {
            assert(inbox_ != nullptr);

            using namespace std::literals::string_view_literals;
            translator_.Insert({ "shutdown"sv, Translator::CreateHandle<command::Shutdown>(*this) });
        }

        static std::string ReadLn() {
            std::string buffer;
            std::lock_guard<std::mutex> lock { in_ };
            std::getline(std::cin, buffer);
            return buffer;
        }

        static void ReadLn(std::string& buffer) {
            std::lock_guard<std::mutex> lock { in_ };
            std::getline(std::cin, buffer);
        }

        template<class ...Args>
        static void Write(Args&&...args) {
            std::lock_guard<std::mutex> lock { out_ };
            ((std::cout << std::forward<Args>(args) << " "), ...);
        }

        // blocks execution thread
        void Run() {
            using namespace std::literals;

            static constexpr std::string_view kDelimiter { " " };
            std::string buffer;
            while (running_) {
                ReadLn(buffer);
                std::string_view input { buffer };
                input = utils::Trim(input);
                // parse buffer
                std::vector<std::string_view> args;
                auto delimiter = input.find(kDelimiter);
                while (delimiter != std::string_view::npos) {
                    args.emplace_back(input.data(), delimiter);
                    // remove prefix with delimiter
                    input.remove_prefix(delimiter + 1);
                    // remove all special characters from the input
                    input = utils::Trim(input);
                    // update delimiter position
                    delimiter = input.find(kDelimiter);
                }
                args.emplace_back(input);
                
                assert(!args.empty() && "Args list can't be empty");

                if (!args.front().empty()) {
                    args.front().remove_prefix(1);
                }

                if (auto handle = translator_.GetHandle(args.front()); handle) {
                    Write("Call handle:", args.front(), '\n');
                    // try to proccess command here
                    std::invoke(*handle, Translator::Params{++args.begin(), args.end()});
                }
                else {
                    // can not recognize the command, pass it to other services
                    command::RawCommand raw  { 
                        std::string { args.front() }, 
                        std::vector<std::string>{ ++args.begin(), args.end() }
                    };
                    if (!inbox_->TryPush(std::move(raw))) {
                        // abandon the command
                        Write("fail to proccess command: command storage is full\n");
                    }
                }

                Write("  -> parsed: [ "sv, args.front(), ", ... ]\n"sv);
            }
        }

        template<class Command>
        void Execute(Command&& cmd) {
            if constexpr (std::is_same_v<Command, command::Shutdown>) {
                assert(inbox_ != nullptr && "Queue can not be NULL");
                inbox_->DisableSentinel();
                running_ = false;
            }
        }

    private:
        CcQueue<command::RawCommand> * const inbox_ { nullptr };
        Translator translator_ {};

        bool running_ { true };

        static inline std::mutex in_ {};
        static inline std::mutex out_ {};
    };
    
    // service
    class Blizzard : public std::enable_shared_from_this<Blizzard> {
    public:
        Blizzard(std::shared_ptr<ssl::context> ssl) 
            : context_ { std::make_shared<boost::asio::io_context>() }
            , work_ { context_->get_executor() }
            , sslContext_ { ssl }
        {
        }

        Blizzard(const Blizzard&) = delete;
        Blizzard(Blizzard&&) = delete;
        Blizzard& operator=(const Blizzard&) = delete;
        Blizzard& operator=(Blizzard&&) = delete;

        ~Blizzard() {
            temp::Console::Write("~Blizzard()\n");
            for (auto& t: threads_) t.join();
        }

        template<typename Command,
            typename = std::enable_if_t<command::details::is_blizzard_api_v<Command>>
        >
        void Execute([[maybe_unused]] Command& cmd) {
            if constexpr (std::is_same_v<Command, command::RealmID>) {
                auto initiateRealmQuery = [weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        self->QueryRealm([weak](size_t realmId) {
                            if (auto self = weak.lock(); self) {
                                temp::Console::Write("ID acquired:", realmId, '\n');
                            }
                        });
                    }
                };
                if (!token_.IsValid()) {
                    AcquireToken(std::move(initiateRealmQuery));
                }
                else {
                    std::invoke(initiateRealmQuery);
                }
            }
            else if (std::is_same_v<Command, command::RealmStatus>) {
                auto initiateRealmQuery = [weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        self->QueryRealm([weak](size_t realmId) {
                            if (auto self = weak.lock(); self) {
                                temp::Console::Write("ID acquired: ", realmId, '\n');
                                self->QueryRealmStatus(realmId, [weak]() {
                                    if (auto self = weak.lock(); self) {
                                        temp::Console::Write("Realm confirmed!\n");
                                    }
                                });
                            }
                        });
                    }
                };
                if (!token_.IsValid()) {
                    AcquireToken(std::move(initiateRealmQuery));
                }
                else {
                    std::invoke(initiateRealmQuery);
                }
            }
            else if (std::is_same_v<Command, command::AccessToken>) {
                AcquireToken([weak = weak_from_this()]() {
                    if (auto self = weak.lock(); self) {
                        temp::Console::Write("Token acquired.\n");
                    }
                });
            }
            else {
                assert(false && "Unreachable: Unknown blizzard command");
            }
        }

        void ResetWork() {
            work_.reset();
        }

        void Run() {
            for (size_t i = 0; i < kThreads; i++) {
                threads_.emplace_back([this](){
                    for (;;) {
                        try {
                            context_->run();
                            break;
                        }
                        catch (std::exception& ex) {
                            // some system exception
                            temp::Console::Write("[ERROR]: ", ex.what(), '\n');
                        }
                    }
                });
            }
        }

        void QueryRealm(std::function<void(size_t realmId)> continuation = {}) const {
            const char * const kHost = "eu.api.blizzard.com";
            auto request = blizzard::Realm(token_.Get()).Build();
            auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    const auto [head, body] = origin->AcquireResponse();
                    rapidjson::Document reader; 
                    reader.Parse(body.data(), body.size());
                    const auto realmId = reader["id"].GetUint64();

                    if (auto service = self.lock(); service) {
                        temp::Console::Write("Realm id: [", realmId, "]\n");
                        if (callback) {
                            boost::asio::post(*service->context_, std::bind(callback, realmId));
                        }
                    }
                }
                else {
                    assert(false && "Unreachable. For now you can invoke this function only synchroniously");
                }
            });
        }

        void QueryRealmStatus(size_t realmId, std::function<void()> continuation = {}) const {
            constexpr char * const kHost = "eu.api.blizzard.com";
            auto request = blizzard::RealmStatus(realmId, token_.Get()).Build();
            auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    const auto [head, body] = origin->AcquireResponse();

                    if (auto service = self.lock(); service) {
                        temp::Console::Write("Body:\n", body, "\n");
                        if (callback) {
                            boost::asio::post(*service->context_, callback);
                        }
                    }
                }
                else {
                    assert(false && "Unreachable. For now you can invoke this function only synchroniously");
                }
            });
        }

        void AcquireToken(std::function<void()> continuation = {}) {
            config_.Read();
            auto request = blizzard::CredentialsExchange(config_.id_, config_.secret_).Build();

            constexpr char * const kHost = "eu.battle.net";
            auto connection = std::make_shared<Connection>(context_, sslContext_, GenerateId(), kHost);

            connection->Write(request, [self = weak_from_this()
                , callback = std::move(continuation)
                , connection = connection->weak_from_this()
            ]() mutable {
                if (auto origin = connection.lock(); origin) {
                    auto [head, body] = origin->AcquireResponse();
                    rapidjson::Document reader; 
                    reader.Parse(body.data(), body.size());
                    
                    std::string token = reader["access_token"].GetString();
                    const auto tokenType = reader["token_type"].GetString();
                    const auto expires = reader["expires_in"].GetUint64();

                    [[maybe_unused]] constexpr auto expectedDuration = 24 * 60 * 60 - 1;
                    assert(expires >= expectedDuration && "Unexpected duration. Blizzard API may be changed!");
                    assert(!strcmp(tokenType, "bearer") && "Unexpected token type. Blizzard API may be changed!");

                    if (auto service = self.lock(); service) {
                        temp::Console::Write("Extracted token: [", token, "]\n");
                        service->token_.Emplace(std::move(token), Token::Duration_t(expires));

                        if (callback) {
                            boost::asio::post(*service->context_, callback);
                        }
                    }
                }
                else {
                    assert(false && "Unreachable. For now you can invoke this function only synchroniously");
                }
            });

        }

    private:
        using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
        
        size_t GenerateId() const {
            return Blizzard::lastID_++;
        }


        static constexpr size_t kThreads { 2 };
        std::vector<std::thread> threads_;

        Token token_;
        std::shared_ptr<boost::asio::io_context> context_;
        Work work_;
        std::shared_ptr<ssl::context> sslContext_;

        Config config_;

        // connection id
        static inline size_t lastID_ { 0 };
    };

}

class App {
public:
    App() 
        : commands_ { kSentinel }
        , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
        , blizzard_ { std::make_shared<temp::Blizzard>(ssl_) }
        , console_ { &commands_ }
    {
        const char * const kVerifyFilePath = "DigiCertHighAssuranceEVRootCA.crt.pem";
        /**
         * [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
         * Cert Chain:
         * DigiCert High Assurance EV Root CA => DigiCert SHA2 High Assurance Server CA => *.battle.net
         * So root cert is DigiCert High Assurance EV Root CA;
         * Valid until: 10/Nov/2031
         * 
         * TODO: read this path from secret + with some chiper
        */
        boost::system::error_code error;
        ssl_->load_verify_file(kVerifyFilePath, error);
        if (error) {
            temp::Console::Write("[ERROR]: ", error.message(), '\n');
        }
        using namespace std::literals::string_view_literals;

        std::initializer_list<Translator::Pair> list {
            {"realm-id"sv,      Translator::CreateHandle<command::RealmID>(*blizzard_) },
            {"realm-status"sv,  Translator::CreateHandle<command::RealmStatus>(*blizzard_) },
            {"token"sv,         Translator::CreateHandle<command::AccessToken>(*blizzard_) }
        };
        translator_.Insert(list);
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
                        temp::Console::Write("  -> queue is empty\n");
                        break;
                    }
                    if (auto handle = translator_.GetHandle(cmd->command_); handle) {
                        std::invoke(*handle, std::vector<std::string_view> { 
                            cmd->params_.begin(), 
                            cmd->params_.end() 
                        });
                    }
                    else {
                        temp::Console::Write("Can not recognize a command:", cmd->command_);
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
    std::vector<std::thread> workers_;
    // common queue
    CcQueue<command::RawCommand> commands_;
    Translator translator_;
    // ssl
    std::shared_ptr<ssl::context> ssl_;
    // services:
    std::shared_ptr<temp::Blizzard> blizzard_;
    temp::Console console_;
};

int main() {
    App app;
    app.Run();
    return 0;
}