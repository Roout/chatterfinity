#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Utility.hpp"
#include "Request.hpp"
#include "Chain.hpp"
#include "Connection.hpp"
#include "Domain.hpp"

#include <stdexcept>

#include "rapidjson/document.h"

namespace domain = blizzard::domain;

namespace service {

Blizzard::Blizzard(const Config *config, Container * outbox) 
    : context_ { std::make_shared<boost::asio::io_context>() }
    , work_ { context_->get_executor() }
    , ssl_ { std::make_shared<ssl::context>(ssl::context::method::sslv23_client) }
    , invoker_ { std::make_unique<Invoker>(this) }
    , config_ { config }
    , outbox_ { outbox }
{
    assert(config_ && "Config is NULL");

    // [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
    // CAs required for secure access to *.battle.net and *.api.blizzard.com
    // TODO: try to avoid loading of CAs
    const char * const kBattleNet = "crt/DigiCertHighAssuranceEVRootCA.crt.pem";
    const char * const kBlizzardApi = "crt/DigiCertGlobalRootCA.crt.pem";
    boost::system::error_code error;
    ssl_->load_verify_file(kBattleNet, error);
    if (error) {
        Console::Write("[blizzard] --error: (*.battle.net CA)", error.message(), '\n');
        error.clear();
    }
    ssl_->load_verify_file(kBlizzardApi, error);
    if (error) {
        Console::Write("[blizzard] --error: (*.api.blizzard.com CA)", error.message(), '\n');
    }
}

Blizzard::~Blizzard() {
    Console::Write("  -> close blizzard service\n");
    // TODO: decide whether I should stop io_context or not?
    // context_->stop();
    for (auto& t: threads_) t.join();
}

void Blizzard::ResetWork() {
    work_.reset();
}

void Blizzard::Run() {
    for (size_t i = 0; i < kThreads; i++) {
        auto worker = [this]() {
            for (;;) {
                try {
                    context_->run();
                    break;
                }
                catch (std::exception& ex) {
                    Console::Write("[blizzard]: "
                        "--error: context raises an exceptiom:"
                        , ex.what(), '\n');
                }
            }
        };
        threads_.emplace_back(std::move(worker));
    }
}

void Blizzard::QueryRealm(Callback continuation) {
    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    const auto& token = cache_[Domain::kToken];
    assert(token.Get<std::string>());
    auto request = blizzard::Realm{ *token.Get<std::string>() }.Build();

    auto readCallback = [weak = utils::WeakFrom<HttpConnection>(connection)
        , service = this
    ]() {
        assert(weak.use_count() > 0);
        auto shared = weak.lock();

        const auto [head, body] = shared->AcquireResponse();
        rapidjson::Document json; 
        json.Parse(body.data(), body.size());
        const auto realmId = json["id"].GetUint64();
        Console::Write("[blizzard] realm id: [", realmId, "]\n");
        // update realm id
        auto& realm = service->cache_[Domain::kRealm];
        realm.Insert<domain::Realm>(
            { realmId }
            , CacheSlot::Duration{ 24 * 60 * 60 });
    };

    auto connect = [connection](Chain::Callback cb) {
        connection->Connect(std::move(cb));
    };
    auto write = [connection, req = std::move(request)](Chain::Callback cb) {
        connection->ScheduleWrite(std::move(req), std::move(cb));
    };
    auto read = [connection](Chain::Callback cb) {
        connection->Read(std::move(cb));
    };

    auto chain = std::make_shared<Chain>(context_);
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback));
    if (continuation) {
        chain->Add(std::move(continuation));
    }
    chain->Execute();
}

void Blizzard::QueryRealmStatus(command::RealmStatus cmd
    , Callback continuation
) {
    // NOTE: can be invalid (was valid before) but not empty!
    const auto& realm = cache_[Domain::kRealm];
    const auto& token = cache_[Domain::kToken];
    assert(token.Get<std::string>());
    assert(realm.Get<domain::Realm>());

    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };
    
    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );
    auto request = blizzard::RealmStatus{ realm.Get<domain::Realm>()->id
        , *token.Get<std::string>() }.Build();

    auto readCallback = [weak = utils::WeakFrom<HttpConnection>(connection)
        , service = this
        , cmd = std::move(cmd)
    ]() {
        assert(weak.use_count() > 0);
        auto shared = weak.lock();

        const auto [head, body] = shared->AcquireResponse();

        std::string message;
        if (domain::RealmStatus response; 
            !domain::Parse(body, response)) 
        {
            message = "sorry, can't provide the answer. Try later please!";
            Console::Write("[blizzard] can't parse response: [", body, "]\n");
        }
        else {
            message = domain::to_string(response);
            auto& cached = service->cache_[Domain::kRealm];
            // cache realm's data
            domain::Realm realm { 
                cached.Get<domain::Realm>()->id
                , response.name
                , response.queue
                , response.status
            };
            cached.Insert(std::move(realm), CacheSlot::Duration{24 * 60 * 60 });
        }
        
        // TODO: update this temporary solution base on IF
        if (cmd.user_.empty()) { // the source of the command is console
            Console::Write("[blizzard] recv:", message, '\n');
        }
        else { // the source of the command is twitch
            message = "@" + cmd.user_ + ", " + message;
            command::RawCommand raw { "chat", { 
                command::ParamData { "channel", cmd.channel_ }
                , { "message", std::move(message) }}
            };
            if (!service->outbox_->TryPush(std::move(raw))) {
                Console::Write("[blizzard] fail to push !realm-status"
                    " response to queue: it is full\n");
            }
        }
    };

    auto connect = [connection](Chain::Callback cb) {
        connection->Connect(std::move(cb));
    };
    auto write = [connection, request = std::move(request)](Chain::Callback cb) {
        connection->ScheduleWrite(std::move(request), std::move(cb));
    };
    auto read = [connection](Chain::Callback cb) {
        connection->Read(std::move(cb));
    };

    auto chain = std::make_shared<Chain>(context_);
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback));
    if (continuation) {
        chain->Add(std::move(continuation));
    }
    chain->Execute();
}

void Blizzard::AcquireToken(Callback continuation) {
    constexpr const char * const kHost { "eu.battle.net" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );
    const Config::Identity identity { "blizzard" };

    const auto secret = GetConfig()->GetSecret(identity);
    if (!secret) { 
        throw std::runtime_error(
            "Cannot find a service with identity = blizzard");
    }
    
    auto request = blizzard::CredentialsExchange(secret->id_, secret->secret_).Build();
    
    auto readCallback = [weak = utils::WeakFrom<HttpConnection>(connection)
        , service = this
    ]() {
        assert(weak.use_count() > 0);
        auto shared = weak.lock();
        const auto [head, body] = shared->AcquireResponse();
        
        domain::Token token;
        if (!domain::Parse(body, token)) {
            Console::Write("[blizzard] --error: Cannot parse token!\n");
        }

        Console::Write("[blizzard] "
            "extracted token: [", domain::to_string(token), "]\n");
        auto& cached = service->cache_[Domain::kToken];
        cached.Insert<std::string>(std::move(token.content)
            , CacheSlot::Duration(token.expires));
    };

    auto connect = [connection](Chain::Callback cb) {
        connection->Connect(std::move(cb));
    };
    auto write = [connection, request = std::move(request)](Chain::Callback cb) {
        connection->ScheduleWrite(std::move(request), std::move(cb));
    };
    auto read = [connection](Chain::Callback cb) {
        connection->Read(std::move(cb));
    };

    auto chain = std::make_shared<Chain>(context_);
    (*chain).Add(std::move(connect))
        .Add(std::move(write))
        .Add(std::move(read), std::move(readCallback));
    if (continuation) {
        chain->Add(std::move(continuation));
    }
    chain->Execute();
}

void Blizzard::Invoker::Execute(command::RealmID) {
    auto completionToken = [blizzard = blizzard_]() {
        const auto& realm = blizzard->cache_[Domain::kRealm];
        assert(realm.Get<domain::Realm>());
        Console::Write("[blizzard] acquire realm id:"
            , realm.Get<domain::Realm>()->id, '\n');
    };

    if (const auto& realm = blizzard_->cache_[Domain::kRealm];
        realm.IsValid()) 
    {
        assert(realm.Get<domain::Realm>());
        std::invoke(completionToken);
        return;
    }

    auto chain = std::make_shared<Chain>(blizzard_->context_);
    if (const auto& token = blizzard_->cache_[Domain::kToken];
        !token.IsValid()) 
    {
        chain->Add([blizzard = blizzard_](Chain::Callback cb) {
            blizzard->AcquireToken(std::move(cb));
        });
    }
    chain->Add([blizzard = blizzard_](Chain::Callback cb) {
        blizzard->QueryRealm(std::move(cb));
    });
    chain->Add(std::move(completionToken));
    chain->Execute();
}

void Blizzard::Invoker::Execute(command::Arena command) {
    Console::Write("[blizzard] arena: [ initiator ="
        , command.user_, ", channel ="
        , command.channel_, ", player ="
        , command.player_, "]\n");

    auto handleResponse = [service = blizzard_, cmd = std::move(command)]() {
        const auto& arena = service->cache_[Domain::kArena];
        assert(arena.Get<domain::Arena>());

        std::string message;
        if (const auto& teams = arena.Get<domain::Arena>()->teams; 
            teams.empty()
        ) {
            message = "Sorry, can't provide the answer. Try later please!";
        }
        else {
            // player name is not provided
            if (!cmd.player_.empty()) {
                auto search = [&player = cmd.player_](const domain::Team& team) {
                    auto it = std::find_if(team.playerNames.cbegin()
                        , team.playerNames.cend()
                        , std::bind(utils::utf8::IsEqual, player, std::placeholders::_1));
                    return it != team.playerNames.cend();
                };
                
                if (auto it = std::find_if(teams.cbegin(), teams.cend(), search);
                    it == teams.cend()
                ) {
                    message = "Sorry, no team has a player with '" 
                        + cmd.player_ + "' nick!";
                }
                else {
                    std::stringstream ss;
                    ss << "Team: " << it->name 
                        << "; Rank: " << it->rank 
                        << "; Rating: " << it->rating << ".";
                    message = ss.str();
                }
                Console::Write("[blizzard]:", message, "\n");
            }
            else {
                // Create default message with top-1 team
                message = domain::to_string(teams.front());
                Console::Write("[blizzard] arena teams:", teams.size()
                    , "; first 2x2 rating:", message, "\n");
            }
        }

        // TODO: update this temporary solution base on IF
        if (!cmd.user_.empty() && !cmd.channel_.empty()) {
            message = "@" + cmd.user_ + ", " + message;
            Console::Write("[blizzard] send message:", message, "\n");

            command::RawCommand raw { "chat", { 
                command::ParamData { "channel", cmd.channel_ }
                , { "message", std::move(message) }}
            };
            
            if (!service->outbox_->TryPush(std::move(raw))) {
                Console::Write("[blizzard] fail to push !arena "
                    "response to queue: it is full\n");
            }
        }
    };

    if (const auto& arena = blizzard_->cache_[Domain::kArena];
        arena.IsValid()) 
    {
        // just use info from the cache
        std::invoke(handleResponse);
        return;
    }

    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        blizzard_->context_, blizzard_->ssl_, 
        kHost, kService, blizzard_->GenerateId()
    );
    
    auto connect = [connection](Chain::Callback cb) {
        connection->Connect(std::move(cb));
    };

    auto write = [connection, service = blizzard_](Chain::Callback cb) {
        const auto& tokenSlot = service->cache_[Domain::kToken];
        const auto& token = *tokenSlot.Get<std::string>();
        
        constexpr uint64_t kSeason { 1 };
        constexpr uint64_t kTeamSize { 2 };
        auto request = blizzard::Arena(kSeason, kTeamSize, token).Build();
        connection->ScheduleWrite(std::move(request), std::move(cb));
    };

    auto read = [connection](Chain::Callback cb) {
        connection->Read(std::move(cb));
    };

    auto readCallback = [weak = utils::WeakFrom<HttpConnection>(connection)
        , service = this->blizzard_]() 
    {
        assert(weak.use_count() > 0);
        auto shared = weak.lock();
        const auto [head, body] = shared->AcquireResponse();
        if (head.statusCode_ != 200) {
            Console::Write("[blizzard] can't get response: body = \"", body, "\"\n");
            constexpr std::chrono::seconds kLifetime { 30 * 60 };
            auto& arena = service->cache_[Domain::kArena];
            // emplace empty arena to not repeat the request too frequently
            arena.Insert(domain::Arena{}, kLifetime);
        }

        domain::Arena response;
        if (!domain::Parse(body, response)) {
            Console::Write("[blizzard] can't parse fully arena response\n");
        }
        else {
            Console::Write("[blizzard] parsed arena response successfully\n");
        }
        constexpr std::chrono::seconds kLifetime { 1 * 60 * 60 };
        auto& arena = service->cache_[Domain::kArena];
        // Can emplace partially parsed arena to not repeat the request too frequently
        // because Blizzard API may be changed
        arena.Insert(std::move(response), kLifetime);
    };

    auto chain = std::make_shared<Chain>(blizzard_->context_);
    if (auto const& token = blizzard_->cache_[Domain::kToken];
        !token.IsValid()) 
    {
        chain->Add([service = blizzard_](Chain::Callback cb) {
            service->AcquireToken(std::move(cb));
        });
    }
    chain->Add(std::move(connect));
    chain->Add(std::move(write));
    chain->Add(std::move(read), std::move(readCallback));
    chain->Add(std::move(handleResponse));
    chain->Execute();
}

void Blizzard::Invoker::Execute(command::RealmStatus cmd) {
    auto chain = std::make_shared<Chain>(blizzard_->context_);

    if (auto const& token = blizzard_->cache_[Domain::kToken];
        !token.IsValid()) 
    {
        // 1. Acquire token
        chain->Add([blizzard = blizzard_](Chain::Callback cb) {
            // `cb` is used as a signal that the initiated 
            // async operation is already completed
            blizzard->AcquireToken(std::move(cb));
        });
    }

    if (auto const& realm = blizzard_->cache_[Domain::kRealm];
        !realm.IsValid()) 
    {
        // 2. Get Realm ID
        chain->Add([blizzard = blizzard_](Chain::Callback cb) {
            blizzard->QueryRealm(std::move(cb));
        });
        // 3. Notify about Realm ID
        chain->Add([&realm](){
            assert(realm.Get<domain::Realm>());

            Console::Write("[blizzard] acquired realm id:", 
                realm.Get<domain::Realm>()->id, '\n');
        });
    }

    // 4. Get Realm's data (despite the fact that Realm's status may 
    // be already acquired). This information must be updated on demand!
    chain->Add([blizzard = blizzard_, cmd = std::move(cmd)](Chain::Callback cb) {
        blizzard->QueryRealmStatus(std::move(cmd), std::move(cb));
    });
    // 5. Notify about request completion
    chain->Add([]() {
        Console::Write("[blizzard] completed realm status request\n");
    });
    chain->Execute();
}

void Blizzard::Invoker::Execute(command::AccessToken) {
    blizzard_->AcquireToken([]() {
        Console::Write("[blizzard] token acquired.\n");
    });
}


} // service