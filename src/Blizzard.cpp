#include "Blizzard.hpp"
#include "Console.hpp"
#include "Config.hpp"
#include "Utility.hpp"
#include "Request.hpp"
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

template<class Encoding, class Allocator>
std::string Serialize(const rapidjson::GenericValue<Encoding, Allocator>& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return { buffer.GetString(), buffer.GetSize() };
}

struct RealmStatusResponse final {
    std::string name;
    std::string queue;
    std::string status;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());
        if (!json.HasMember("realms")) return false;
        const auto realms = json["realms"].GetArray();
        assert(!realms.Empty() && "Empty realms");

        // TODO: don't take just first value
        const auto& front = *realms.Begin();
        if (!front.HasMember("name")) return false;
        name = front["name"].GetString();

        if (!json.HasMember("has_queue")) return false;
        const auto hasQueue = json["has_queue"].GetBool();
        queue = hasQueue? "has queue": "no queue";

        if (!json.HasMember("status") || !json["status"].HasMember("type")) return false;
        status = json["status"]["type"].GetString();

        return true;
    }

};

std::string to_string(const RealmStatusResponse& realm) {
    return realm.name + "(" + realm.status + "): " + realm.queue;
}

struct TokenResponse final {
    std::string content;
    std::string type;
    std::uint64_t expires;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());

        if (!::Copy(json, content, "access_token")) return false;
        if (!::Copy(json, type, "token_type")) return false;
        if (!::Copy(json, expires, "expires_in")) return false;

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
    for (const auto& player: team.players) ss << " " << std::quoted(player, '\'');
    return ss.str();
}

struct ArenaResponse final {
    std::vector<Team> teams;

    bool Parse(const std::string& buffer) {
        rapidjson::Document json; 
        json.Parse(buffer.data(), buffer.size());

        auto entries_it = json.FindMember("entries");
        if (entries_it == json.MemberEnd()) return false;
        const auto& entries = entries_it->value.GetArray();
        for (auto&& entry: entries) {
            Team team;
            if (!::Copy(entry, team.rank, "rank")) return false;
            if (!::Copy(entry, team.rating, "rating")) return false;

            auto team_it = entry.FindMember("team");
            if (team_it == entry.MemberEnd()) return false;
            const auto& teamValue = team_it->value;
            { // team : {
                if (!::Copy(teamValue, team.name, "name")) return false;
            
                { // realm : {
                    auto realm_it = teamValue.FindMember("realm");
                    if (realm_it == teamValue.MemberEnd()) return false;
                    if (!::Copy(realm_it->value, team.realm, "slug")) return false;
                } // } realm

                auto members_it = teamValue.FindMember("members");
                if (members_it != teamValue.MemberEnd()) { // members: { 
                    const auto& members = members_it->value.GetArray();
                    
                    team.players.resize(members.Size());
                    size_t i = 0;
                    for (auto&& player: members) {
                        auto character_it = player.FindMember("character");
                        if (character_it == player.MemberEnd()) return false;
                        if (!::Copy(character_it->value, team.players[i], "name")) return false;
                        ++i;
                    }
                } // } members 
            } // } team
            teams.emplace_back(std::move(team));
        }
        return true;
    }
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

    const char * const kVerifyFilePath = "crt/DigiCertHighAssuranceEVRootCA.crt.pem";
    // [DigiCert](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
    // TODO: read this path from secret + with some chiper
    boost::system::error_code error;
    ssl_->load_verify_file(kVerifyFilePath, error);
    if (error) {
        Console::Write("[blizzard] [ERROR]:", error.message(), '\n');
    }
}

Blizzard::~Blizzard() {
    Console::Write("  -> close blizzard service\n");
    // TODO: 
    // - [ ] close io_context? context_->stop();
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
                    // some system exception
                    Console::Write("[ERROR]: ", ex.what(), '\n');
                }
            }
        });
    }
}

void Blizzard::QueryRealm(std::function<void(size_t realmId)> continuation) {
    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );
    assert(token_.IsValid());
    blizzard::Realm request{ *token_.Get<std::string>() };
    auto onConnect = [request = request.Build()
        , service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1 && 
            "Fail invariant:"
            "Expected: 1 ref - instance which is executing Connection::OnWrite"
            "Assertion Failure may be caused by changing the "
            "(way)|(place where) this callback is being invoked"
        );
        auto shared = connection.lock();
        shared->ScheduleWrite(std::move(request), [service
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1);

            auto OnReadSuccess = [service
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);
                
                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();
                rapidjson::Document json; 
                json.Parse(body.data(), body.size());
                const auto realmId = json["id"].GetUint64();
                Console::Write("[blizzard] realm id: [", realmId, "]\n");
                if (callback) {
                    boost::asio::post(*service->context_, std::bind(callback, realmId));
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::QueryRealmStatus(size_t realmId
    , command::RealmStatus cmd
    , std::function<void()> continuation
) {
    constexpr const char * const kHost { "eu.api.blizzard.com" };
    constexpr const char * const kService { "https" };
    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    assert(token_.IsValid());
    blizzard::RealmStatus request{ realmId, *token_.Get<std::string>() };
    auto onConnect = [request = request.Build()
        , cmd = std::move(cmd)
        , service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1 && 
            "Fail invariant:"
            "Expected: 1 ref - instance which is executing Connection::OnWrite"
            "Assertion Failure may be caused by changing the "
            "(way)|(place where) this callback is being invoked"
        );

        auto shared = connection.lock();
        shared->ScheduleWrite(std::move(request), [service
            , cmd = std::move(cmd)
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1);

            auto OnReadSuccess = [service
                , cmd = std::move(cmd)
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);

                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();

                RealmStatusResponse realm;
                std::string message;
                if (!realm.Parse(body)) {
                    message = "sorry, can't provide the answer. Try later please!";
                    Console::Write("[blizzard] can't parse response: [", body, "]\n");
                }
                else {
                    message = to_string(realm);
                }
                
                // TODO: update this temporary solution base on IF
                if (cmd.initiator_.empty()) { // the sourceof the command is 
                    Console::Write("[blizzard] recv:", message, '\n');
                }
                else {
                    message = "@" + cmd.initiator_ + ", " + message;
                    command::RawCommand raw { "chat", { std::move(cmd.channel_)
                        , std::move(message) } };
                    if (!service->outbox_->TryPush(std::move(raw))) {
                        Console::Write("[blizzard] fail to push !realm-status"
                            " response to queue: it is full\n");
                    }
                }
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };
            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::AcquireToken(std::function<void()> continuation) {
    constexpr const char * const kHost { "eu.battle.net" };
    constexpr const char * const kService { "https" };

    auto connection = std::make_shared<HttpConnection>(
        context_, ssl_ , kHost, kService, GenerateId()
    );

    auto onConnect = [service = this
        , callback = std::move(continuation)
        , connection = utils::WeakFrom<HttpConnection>(connection)
    ]() {
        assert(connection.use_count() == 1);
        
        const Config::Identity identity { "blizzard" };
        const auto secret = service->GetConfig()->GetSecret(identity);
        if (!secret) { 
            throw std::runtime_error("Cannot find a service with identity = blizzard");
        }
        auto request = blizzard::CredentialsExchange(secret->id_, secret->secret_).Build();
        auto shared = connection.lock();

        shared->ScheduleWrite(std::move(request), [service
            , callback = std::move(callback)
            , connection
        ]() {
            assert(connection.use_count() == 1 && 
                "Fail invariant:"
                "Expected: 1 ref - instance which is executing Connection::OnWrite"
                "Assertion Failure may be caused by changing the "
                "(way)|(place where) this callback is being invoked"
            );

            auto OnReadSuccess = [service
                , callback = std::move(callback)
                , connection
            ]() mutable {
                assert(connection.use_count() == 1);

                auto shared = connection.lock();
                const auto [head, body] = shared->AcquireResponse();
                
                TokenResponse token;
                token.Parse(body);

                Console::Write("[blizzard] extracted token: ["
                    , to_string(token), "]\n");
                service->token_.Emplace<std::string>(std::move(token.content)
                    , CacheSlot::Duration(token.expires));
                
                if (callback) {
                    boost::asio::post(*service->context_, callback);
                }
            };

            auto shared = connection.lock();
            shared->Read(std::move(OnReadSuccess));
        });
    };
    connection->Connect(std::move(onConnect));
}

void Blizzard::Invoker::Execute(command::RealmID) {
    auto initiateRealmQuery = [blizzard = blizzard_]() {
        blizzard->QueryRealm([](size_t realmId) {
            Console::Write("[blizzard] acquire realm id:", realmId, '\n');
        });
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateRealmQuery));
    }
    else {
        std::invoke(initiateRealmQuery);
    }
}

void Blizzard::Invoker::Execute(command::Arena command) {
    auto handleResponse = [service = blizzard_](const command::Arena& cmd) {
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
            if (!cmd.param_.empty()) {
                auto it = std::find_if(teams.cbegin(), teams.cend(), 
                    [&player = cmd.param_](const Team& team) {
                        auto it = std::find_if(team.players.cbegin(), team.players.cend()
                            , std::bind(utils::IsEqualUtf8, player, std::placeholders::_1));
                        return it != team.players.cend();
                    });
                if (it == teams.cend()) {
                    message = "Sorry, no team has a player with '" + cmd.param_ + "' nick!";
                }
                else {
                    std::stringstream ss;
                    ss << "Team: " << it->name << "; Rank: " << it->rank << "; Rating: " << it->rating << ".";
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
        if (!cmd.initiator_.empty() && !cmd.channel_.empty()) {
            message = "@" + cmd.initiator_ + ", " + message;
            Console::Write("[blizzard] send message:", message, "\n");
            command::RawCommand raw { "chat"
                , { cmd.channel_, std::move(message) }};
            
            if (!service->outbox_->TryPush(std::move(raw))) {
                Console::Write("[blizzard] fail to push !arena "
                    "response to queue: it is full\n");
            }
        }
    };

    auto queryArena = [blizzard = blizzard_
        , cmd = command
        , callback = handleResponse]() 
    {
        Console::Write("[blizzard] arena: [ initiator =",
             cmd.initiator_, ", channel =", cmd.channel_, ", param =", cmd.param_, "]\n");

        constexpr const char * const kHost { "eu.api.blizzard.com" };
        constexpr const char * const kService { "https" };
        constexpr uint64_t kSeason { 1 };
        constexpr uint64_t kTeamSize { 2 };

        auto connection = std::make_shared<HttpConnection>(
            blizzard->context_, blizzard->ssl_
            , kHost, kService, blizzard->GenerateId()
        );
        
        assert(blizzard->token_.IsValid());
        auto token = *blizzard->token_.Get<std::string>();
        auto arena = blizzard::Arena(kSeason, kTeamSize, token).Build();
        auto onConnect = [request = std::move(arena)
            , callback = std::move(callback)
            , service = blizzard
            , cmd = std::move(cmd)
            , connection = utils::WeakFrom<HttpConnection>(connection)
        ]() {
            assert(connection.use_count() == 1 && 
                "Fail invariant:"
                "Expected: 1 ref - instance which is executing Connection::OnWrite"
                "Assertion Failure may be caused by changing the "
                "(way)|(place where) this callback is being invoked"
            );
            auto shared = connection.lock();
            shared->ScheduleWrite(std::move(request), [service
                , callback = std::move(callback)
                , cmd = std::move(cmd)
                , connection
            ]() {
                assert(connection.use_count() == 1);

                auto OnReadSuccess = [service
                    , callback = std::move(callback)
                    , connection
                    , cmd = std::move(cmd)
                ]() mutable {
                    assert(connection.use_count() == 1);
                    auto shared = connection.lock();

                    const auto [head, body] = shared->AcquireResponse();
                    if (head.statusCode_ != 200) {
                        Console::Write("[blizzard] can't get response: body = \"", body, "\"\n");
                        constexpr std::chrono::seconds kLifetime { 30 * 60 };
                        // emplace empty arena to not repeat the request too frequently
                        service->arena_.Emplace(ArenaResponse{}, kLifetime);
                    }

                    ArenaResponse arena;
                    if (!arena.Parse(body)) {
                        Console::Write("[blizzard] can't parse fully arena response\n");
                    }
                    else {
                        Console::Write("[blizzard] parsed arena response successfully\n");
                    }
                    constexpr std::chrono::seconds kLifetime { 1 * 60 * 60 };
                    // Can emplace partially parsed arena to not repeat the request too frequently
                    // because Blizzard API may be changed
                    service->arena_.Emplace(std::move(arena), kLifetime);

                    std::invoke(callback, cmd);
                };

                auto shared = connection.lock();
                shared->Read(std::move(OnReadSuccess));
            });
        };
        connection->Connect(std::move(onConnect));
    };

    if (!blizzard_->arena_.IsValid()) {
        if (!blizzard_->token_.IsValid()) {
            blizzard_->AcquireToken(std::move(queryArena));
        }
        else {
            std::invoke(queryArena);
        }
    }
    else {
        // just use info from the cache
        std::invoke(handleResponse, command);
    }
}

void Blizzard::Invoker::Execute(command::RealmStatus cmd) {
    auto initiateQuery = [blizzard = blizzard_, cmd = std::move(cmd)]() {
        blizzard->QueryRealm([blizzard, cmd = std::move(cmd)](size_t realmId) {
            Console::Write("[blizzard] acquire realm id:", realmId, '\n');
            blizzard->QueryRealmStatus(realmId, std::move(cmd), []() {
                Console::Write("[blizzard] complete realm status request\n");
            });
        });
    };
    if (!blizzard_->token_.IsValid()) {
        blizzard_->AcquireToken(std::move(initiateQuery));
    }
    else {
        std::invoke(initiateQuery);
    }
}

void Blizzard::Invoker::Execute(command::AccessToken) {
    blizzard_->AcquireToken([]() {
        Console::Write("[blizzard] token acquired.\n");
    });
}


} // service