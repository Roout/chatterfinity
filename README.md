# Chatterfinity

Chat bot will provide an information from the WOW database through chat in twitch. Bot is in development. Other functionality will be added later and is not planned yet.

## Remote services

- Discord
- [Twitch](https://dev.twitch.tv/docs/irc)
- [World of Warcraft API](https://develop.battle.net/documentation/world-of-warcraft-classic/game-data-apis)

## Dependencies

- OpenSSL 1.1.1L
- Rapidjson
- Boost.Asio 1.72.0

## Quick Start

1. Clone <https://github.com/Roout/chatterfinity.git>
2. Create `secret/services.json` file
3. Build Project with CMake 3.12 or higher

### Configuration

- The example of the file is provided. **See** [services-example.json](secret/services.json).  
- You can get `client_id` and `secret` in [Twitch Developer Console](https://dev.twitch.tv/) and [Blizzard Developer Console](https://develop.battle.net/).
- **Note, secret** is required only for blizzard [Client Credential Flow](https://develop.battle.net/documentation/guides/using-oauth/client-credentials-flow) and **MUST NOT** be provided to any other party.  
- You can get `token` for twitch setting up local [server](https://github.com/Roout/twitch-token) and opening it in browser at "http://localhost:3000". **See** more information at the page [twitch-token generator](https://github.com/Roout/twitch-token).

## Commands

Commands can be invoked within either Console either twitch chat. Chat messages can be russian.

Bot supports UTF8 charset:

- Windows (tested on Windows 10)
- Linux (not tested yet)

### Twitch chat

For now you can invoke the following commands within channel chat:

| name           | description                                        |
|----------------|----------------------------------------------------|
| `!realm-status`| Show flamegor server status and queue information  |
| `!arena`       | Show current top 1 EU region                       |

### Console

For now you can invoke the following commands within console:

| Name           |  Params            | Description                                                        |
|----------------|--------------------|--------------------------------------------------------------------|
| `!realm-status`|                    | Show flamegor server status and queue information to console only  |
| `!realm-id`    |                    | Show flamegor server id to console only                            |
| `!arena`       |                    | Show current top 1 EU to console only                              |
| `!login`       |                    | Login to the `irc.chat.twitch.tv:6697`                             |
| `!join`        | chatroom           | Join the chatroom                                                  |
| `!chat`        | chatroom "message" | Send message to provided chat (message in "")                      |
| `!leave`       | chatroom           | Leave a chatroom                                                   |
| `!pong`        |                    | Send pong to the `irc.chat.twitch.tv:6697`                         |
| `!validate`    |                    | Validate the current twitch token                                  |

## Future features

### Chat bot commands

- [ ] choose creature for user (customized **!dice** roll);
- [ ] arena leaderboard:
  - [x] top 1 team of the provided realm (implemented partialy)
  - [ ] top 1 team of the provided region,
  - [ ] rating for the team by player name,
  - [ ] win-lose statistics for the team by player name;
- [ ] auction (don't know whether it will be usefull)?

Arena teams is the most interesting thing for WOW community so it will be the most important part of the bot.

### Restrictions

There are several very imporatant restriction on the communication in twitch chat. They depend on MODE and Bot authority.
Bot DOESN't consider twitch restrictions yet. Following restriction will be implemented soon:

- Minimum 1 second delay between messages via IRC connection. For slow mode it can be greater.
- 30 messages for 1 second
- Authority e.g., subscriber mode, etc

## Certificate Authorities

The following certificates are downloaded for application to be able to work with several APIs.
They are used by application when connection is trying to complete a handshake.
Connection needs a CA information to verify if the certificate is "valid" or not.
Connection uses (loads for [ssl context](https://www.boost.org/doc/libs/1_72_0/doc/html/boost_asio/reference/ssl__context/load_verify_file.html)) root CA to verify service.

### Blizzard

**Source:** [DigiCert list](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
Root certificate is DigiCert High Assurance EV Root CA;

### Twitch

**Source:** [Amazon CA](https://www.amazontrust.com/repository/)
**Distinguished Name:**  

CN=Starfield Services Root Certificate Authority - G2,  
O=Starfield Technologies\, Inc.,  
L=Scottsdale,  
ST=Arizona,  
C=US  
