#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Utility.hpp"
#include "Request.hpp"
#include "Chain.hpp"
#include "Connection.hpp"

#include <iomanip> // std::quoted
#include <stdexcept>
#include <type_traits>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

namespace {

template<typename T>
struct is_extractable_value {
    static constexpr bool value { 
        std::is_same_v<T, bool>
        || std::is_same_v<T, std::uint64_t>
        || std::is_same_v<T, int>
        || std::is_same_v<T, std::string>
    };
};

// utility function
// works for: bool, string, int, uint64_t
template<typename T, 
    typename = std::enable_if_t<is_extractable_value<T>::value>
>
bool Copy(const rapidjson::Value& src, T& dst, const char *key) {
    if (auto it = src.FindMember(key); it != src.MemberEnd()) {
        if constexpr (std::is_same_v<bool, T>) {
            dst = it->value.GetBool();
            return true;
        }
        else if constexpr (std::is_same_v<std::uint64_t, T>) {
            dst = it->value.GetUint64();
            return true;
        }
        else if constexpr (std::is_same_v<int, T>) {
            dst = it->value.GetInt();
            return true;
        }
        else if constexpr (std::is_same_v<std::string, T>) {
            dst = it->value.GetString();
            return true;
        }
    }
    return false;
}

struct RealmStatusResponse {
    std::string name;
    std::string queue;
    std::string status;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());
        if (!json.HasMember("realms")) { 
            return false;
        }
        const auto realms = json["realms"].GetArray();
        assert(!realms.Empty() && "Empty realms");

        // TODO: check whether the first realm is always 
        // the one we needed
        const auto& front = *realms.Begin();
        if (!front.HasMember("name")) { 
            return false;
        }
        name = front["name"].GetString();

        if (!json.HasMember("has_queue")) { 
            return false;
        }
        const auto hasQueue = json["has_queue"].GetBool();
        queue = hasQueue? "has queue": "no queue";

        if (!json.HasMember("status") || !json["status"].HasMember("type")) { 
            return false;
        }
        status = json["status"]["type"].GetString();

        return true;
    }

};

std::string to_string(const RealmStatusResponse& realm) {
    return realm.name + "(" + realm.status + "): " + realm.queue;
}

struct TokenResponse {
    std::string content;
    std::string type;
    std::uint64_t expires;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());

        if (!::Copy(json, content, "access_token")) {
            return false;
        }
        if (!::Copy(json, type, "token_type")) {
            return false;
        }
        if (!::Copy(json, expires, "expires_in")) {
            return false;
        }

        assert(type == "bearer" 
            && "Unexpected token type. Blizzard API may be changed!");

        return true;
    }

};

std::string to_string(const TokenResponse& token) {
    return token.content;
}

struct Team {
    std::string name; // team name
    std::string realm; // realm slug
    std::vector<std::string> players; // names of players
    int rank;
    int rating;
};

std::string to_string(const Team& team) {
    std::stringstream ss;
    ss << std::quoted(team.name, '\'') << " " << team.realm << " " 
        << std::to_string(team.rank) << " " << std::to_string(team.rating) << " "
        << std::to_string(team.players.size()) << ":";
    for (const auto& player: team.players) {
        ss << " " << std::quoted(player, '\'');
    }
    return ss.str();
}

struct ArenaResponse {
    std::vector<Team> teams;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());

        auto entries_it = json.FindMember("entries");
        if (entries_it == json.MemberEnd()) {
            return false;
        }
        const auto& entries = entries_it->value.GetArray();
        for (auto&& entry: entries) {
            Team team;
            if (!::Copy(entry, team.rank, "rank")) {
                return false;
            }
            if (!::Copy(entry, team.rating, "rating")) {
                return false;
            }

            auto team_it = entry.FindMember("team");
            if (team_it == entry.MemberEnd()) {
                return false;
            }
            const auto& teamValue = team_it->value;
            if (!ParseTeam(teamValue, team)) {
                return false;
            }
            teams.emplace_back(std::move(team));
        }
        return true;
    }

private:
    bool ParseTeam(const rapidjson::Value& teamValue, Team& team) {
        if (!::Copy(teamValue, team.name, "name")) {
            return false;
        }
        // realm
        auto realm_it = teamValue.FindMember("realm");
        if (realm_it == teamValue.MemberEnd()) {
            return false;
        }
        if (!::Copy(realm_it->value, team.realm, "slug")) {
            return false;
        }
        // members
        auto members_it = teamValue.FindMember("members");
        if (members_it == teamValue.MemberEnd()) {
            // team has no members
            return true;
        }
        const auto& members = members_it->value.GetArray();
        
        team.players.resize(members.Size());
        size_t i = 0;
        for (auto&& player: members) {
            auto character_it = player.FindMember("character");
            if (character_it == player.MemberEnd()) {
                return false;
            }
            if (!::Copy(character_it->value, team.players[i], "name")) {
                return false;
            }
            ++i;
        }
        return true;
    }
};

struct Realm {
    std::uint64_t id;
    std::string name;
    std::string queue;
    std::string status;

    Realm(std::uint64_t id)
        : id { id } {}

    Realm(std::uint64_t id, std::string name, std::string queue, std::string status)
        : id { id }
        , name { std::move(name) }
        , queue { std::move(queue) }
        , status { std::move(status) }
    {}
    
};

} // namespace {

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
        threads_.emplace_back([this](){
            for (;;) {
                try {
                    context_->run();
                    break;
                }
                catch (std::exception& ex) {
                    Console::Write(
                        "[blizzard]: --error: context raises an exceptiom:"
                        , ex.what(), '\n');
                }
            }
        });
    }
}

void Blizzard::QueryRealm(Callback continuation) {
    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    assert(token_.IsValid());
    auto request = blizzard::Realm{ *token_.Get<std::string>() }.Build();

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
        service->realm_.Emplace<Realm>({realmId}, CacheSlot::Duration{ 24 * 60 * 60 });
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
    // FIXME: can be valid when the `QueryRealmStatus` is posted to execution
    // but invalidated when being executed!
    assert(token_.IsValid());
    assert(realm_.IsValid());
    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };
    
    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );
    auto request = blizzard::RealmStatus{ realm_.Get<Realm>()->id
        , *token_.Get<std::string>() }.Build();

    auto readCallback = [weak = utils::WeakFrom<HttpConnection>(connection)
        , service = this
        , cmd = std::move(cmd)
    ]() {
        assert(weak.use_count() > 0);
        auto shared = weak.lock();

        const auto [head, body] = shared->AcquireResponse();

        RealmStatusResponse realmResponse;
        std::string message;
        if (!realmResponse.Parse(body)) {
            message = "sorry, can't provide the answer. Try later please!";
            Console::Write("[blizzard] can't parse response: [", body, "]\n");
        }
        else {
            message = to_string(realmResponse);
            // cache realm's data
            Realm realm { service->realm_.Get<Realm>()->id
                , realmResponse.name
                , realmResponse.queue
                , realmResponse.status
            };
            service->realm_.Emplace(std::move(realm), CacheSlot::Duration{24 * 60 * 60 });
        }
        
        // TODO: update this temporary solution base on IF
        if (cmd.user_.empty()) { // the sourceof the command is 
            Console::Write("[blizzard] recv:", message, '\n');
        }
        else {
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
        
        TokenResponse token;
        token.Parse(body);

        Console::Write("[blizzard] extracted token: ["
            , to_string(token), "]\n");
        service->token_.Emplace<std::string>(std::move(token.content)
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
        Console::Write("[blizzard] acquire realm id:"
            , blizzard->realm_.Get<Realm>()->id, '\n');
    };

    if (blizzard_->realm_.IsValid()) {
        assert(blizzard_->realm_.Get<Realm>());
        std::invoke(completionToken);
        return;
    }

    auto chain = std::make_shared<Chain>(blizzard_->context_);
    if (!blizzard_->token_.IsValid()) {
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
        const auto& cache = service->arena_;
        assert(cache.IsValid());

        std::string message;
        if (const auto& teams = cache.Get<ArenaResponse>()->teams; 
            teams.empty()
        ) {
            message = "Sorry, can't provide the answer. Try later please!";
        }
        else {
            // player name is not provided
            if (!cmd.player_.empty()) {
                auto it = std::find_if(teams.cbegin(), teams.cend(), 
                    [&player = cmd.player_](const Team& team) {
                        auto it = std::find_if(team.players.cbegin(), team.players.cend()
                            , std::bind(utils::utf8::IsEqual, player, std::placeholders::_1));
                        return it != team.players.cend();
                    });
                if (it == teams.cend()) {
                    message = "Sorry, no team has a player with '" + cmd.player_ + "' nick!";
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
                message = to_string(teams.front());
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

    if (blizzard_->arena_.IsValid()) {
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
        auto token = *service->token_.Get<std::string>();
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
            // emplace empty arena to not repeat the request too frequently
            service->arena_.Emplace(ArenaResponse{}, kLifetime);
        }

        ArenaResponse response;
        if (!response.Parse(body)) {
            Console::Write("[blizzard] can't parse fully arena response\n");
        }
        else {
            Console::Write("[blizzard] parsed arena response successfully\n");
        }
        constexpr std::chrono::seconds kLifetime { 1 * 60 * 60 };
        // Can emplace partially parsed arena to not repeat the request too frequently
        // because Blizzard API may be changed
        service->arena_.Emplace(std::move(response), kLifetime);
    };

    auto chain = std::make_shared<Chain>(blizzard_->context_);
    if (!blizzard_->token_.IsValid()) {
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

    if (!blizzard_->token_.IsValid()) {
        // 1. Acquire token
        chain->Add([blizzard = blizzard_](Chain::Callback cb) {
            // `cb` is used as a signal that the initiated 
            // async operation is already completed
            blizzard->AcquireToken(std::move(cb));
        });
    }

    if (!blizzard_->realm_.IsValid()) {
        assert(blizzard_->realm_.Get<Realm>());
        // 2. Get Realm ID
        chain->Add([blizzard = blizzard_](Chain::Callback cb) {
            blizzard->QueryRealm(std::move(cb));
        });
        // 3. Notify about Realm ID
        chain->Add([blizzard = blizzard_](){
            Console::Write("[blizzard] acquired realm id:", 
                blizzard->realm_.Get<Realm>()->id, '\n');
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