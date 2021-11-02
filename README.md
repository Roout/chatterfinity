# Chatterfinity

![Linux](https://github.com/Roout/chatterfinity/actions/workflows/cmake.yml/badge.svg?branch=master)

This **Twitch Chat Bot** provides an information about **TBC Arena Leaderboard**, **Realm Status**, etc. Bot uses Blizzard WOW API.  
Bot is in development so some functionality will be added later.

## Remote services

- Discord (not added yet)
- [Twitch API](https://dev.twitch.tv/docs/api)
- [World of Warcraft API](https://develop.battle.net/documentation/world-of-warcraft-classic/game-data-apis)

## Dependencies

- OpenSSL 1.1.1L
- Rapidjson 1.1.0
- Boost.Asio 1.72.0

## Quick Start

1. Clone <https://github.com/Roout/chatterfinity.git>
2. Create `secret/services.json` file
3. Build Project with CMake 3.12 or higher

### Configuration

- The example of the [services-example.json](secret/services.json) is provided.
- You can get `client_id` and `secret` in [Twitch Developer Console](https://dev.twitch.tv/) and [Blizzard Developer Console](https://develop.battle.net/).
- **Secret** is required only for Blizzard [Client Credential Flow](https://develop.battle.net/documentation/guides/using-oauth/client-credentials-flow) and **MUST NOT** be disclosed to any 3rd party.  
- You can get Twitch `token` running [local server](https://github.com/Roout/twitch-token) and opening it in browser at <http://localhost:3000>. More information is provided at the page of the [twitch-token generator](https://github.com/Roout/twitch-token).

## Commands

Commands can be invoked within either Console either Twitch chat. Chat messages can be russian.  
Commands which have already been implemented are quite crude without any customization and flexibility because they are used to test core of the bot now.  
Bot sends messages in the chat using [PRIVMSG](https://dev.twitch.tv/docs/irc/guide) IRC command.

Bot supports [UTF8](https://en.wikipedia.org/wiki/UTF-8) charset:

- Windows (can not print characters which required more than 2 code units (bytes) for encoding)
- Linux (should work even with encoding which uses 4 code units (bytes))

### Twitch chat

For now you can invoke the following custom commands within channel chat:

| Name           |  Params        | Description                                         |
|----------------|----------------|-----------------------------------------------------|
| `!realm-status`|                | Show flamegor server status and queue information   |
| `!arena`       |                | Show current top 1 of the EU region                 |
| `!arena`       | -player "nick" | Show team name, rank, rating of the given player    |

You also can call alias within chat. But you can add alias only within console

```bash
# call already added beforehand alias
!exodus
# the output for alias above will be equal to output of the following arena command
!arena -player "Шаркии"
```

### Console

For now you can invoke the following custom commands within console:

| Name           |  Params            | Description                                                        |
|----------------|--------------------|--------------------------------------------------------------------|
| `!realm-status`|                    | Show flamegor server status and queue <br />information to console only  |
| `!realm-id`    |                    | Show flamegor server id to console only                            |
| `!arena`       |                    | Show current top 1 EU to console only                              |
| `!login`       |                    | Login to the `irc.chat.twitch.tv:6697`                             |
| `!join`        | -channel "chatroom"| Join the chatroom                                                  |
| `!chat`        | -channel "chatroom" -message "message" | Send message to provided chat (message in "")  |
| `!leave`       | -channel "chatroom"| Leave a chatroom                                                   |
| `!pong`        |                    | Send pong to the `irc.chat.twitch.tv:6697`                         |
| `!validate`    |                    | Validate the current twitch token                                  |
| `!alias`       | -alias "alias_name" -command "command" <br />-k1 v1 -k2 v2 ... (other params) | Add alias |

Alias can be added only within console.

```bash
# add alias for displaying arena team data
!alias -alias exodus -command arena -player "Шаркии"
# call alias within console or within twitch chat
!exodus
# the output for alias above will be equal to output of the following arena command
!arena -player "Шаркии"
```

Aliases are being saved in alias.txt with executable. So they can be saved/soaded between sessions. You also can add alias manually there. But this may lead to errors e.g., coincidence of alias name and existing command name. If you do the same within console it won't add such alias.  

**Not implemented:**

- [ ] remove alias
- [ ] print existing aliases

## Future features

### Chat bot commands

- [ ] choose creature for user (customized **!dice** roll)
- [ ] arena leaderboard:
  - [x] top 1 team of the provided realm
  - [ ] top 1 team of the provided region
  - [x] rating for the team by player name from EU regin
  - [ ] win-lose statistics for the team by player name
- [ ] auction (don't know whether it will be usefull)

Arena leaderboard is one of the most interesting thing for WOW community so it will be the most important part of the bot.

### Restrictions

There are several very imporatant restriction on the communication in twitch chat. They depend on MODE and Bot authority.
Bot doesn't consider some twitch restrictions yet.  

**NOT implemented yet:**

- Minimum 1 second delay between messages via IRC connection. For slow mode it can be greater.
- Authority e.g., subscriber mode, etc

**ALREADY implemented:**

- [Rate Limit](https://dev.twitch.tv/docs/irc/guide) is used for **PRIVMSG**, **JOIN**, **PASS**.  
Only 2 buckets with the strictest restrictions supported now: general bucket(**JOIN**, **PASS**), channel bucket(**PRIVMSG**).
Buckets doesn't take into account users authority and assuming this is simple account. Now you're neither moderator neither subscriber. Authority support is not implemented.
Buckets doesn't take into account bans, slowmodes, etc. These features will be implemented later.

If IRC or HTTPS connection fails to connect/read/write it will try to reconnect 3 times with 2s, 4s, 8s timeouts.

## Certificate Authorities

The following certificates are downloaded for application to be able to work with several APIs.
They are used by application when connection is trying to complete a handshake.
Connection needs a CA information to verify if the certificate is "valid" or not.
Connection uses (loads for [SSL Context](https://www.boost.org/doc/libs/1_72_0/doc/html/boost_asio/reference/ssl__context/load_verify_file.html)) root CA to verify a service.

### Blizzard

**Source:** [DigiCert list](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
Root certificate is DigiCert High Assurance EV Root CA;  
Used by **eu.battle.net** (Blizzard Auth)

**Source:** [DigiCert list](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
Root certificate is DigiCert Global Root CA  
Used by **eu.api.blizzard.com** (Blizzard API)

### Twitch

**Source:** [Amazon CA](https://www.amazontrust.com/repository/)
**Distinguished Name:**  

CN=Starfield Services Root Certificate Authority - G2,  
O=Starfield Technologies\, Inc.,  
L=Scottsdale,  
ST=Arizona,  
C=US  
