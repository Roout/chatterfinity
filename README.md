# Chatterfinity

Chat bot will provide an information from the Wow database through chat in twitch.
Other functionality will be added later and is not planned yet.

## Targets

- Discord
- [Twitch Internet Relay Chat (IRC) interface](https://dev.twitch.tv/docs/irc)
- [World of Warcraft API](https://develop.battle.net/documentation/world-of-warcraft-classic/game-data-apis)
  - what creature are you? (random index used to genereate GET /data/wow/creature-family/${index})
  - check realm for queue
  - arena leaderboard
  - auction (don't know whether it will be usefull)?

## Certificate Authorities

The following certificates are downloaded for application to be able to work with several APIs.
They are used by application when connection is trying to complete a handshake.
Connection needs a CA information to verify if the certificate is "valid" or not.
Connection uses (loads for [ssl context](https://www.boost.org/doc/libs/1_72_0/doc/html/boost_asio/reference/ssl__context/load_verify_file.html)) root CA to verify service.

### Blizzard

**Source:** [DigiCert list](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)

**Certificate Chain:**  

- DigiCert High Assurance EV Root CA
- DigiCert SHA2 High Assurance Server CA
- *.battle.net.  

Root certificate is DigiCert High Assurance EV Root CA;
Valid until: 10/Nov/2031

### Twitch

**Source:** [Amazon CA](https://www.amazontrust.com/repository/)
**Distinguished Name:**  

CN=Starfield Services Root Certificate Authority - G2,  
O=Starfield Technologies\, Inc.,  
L=Scottsdale,  
ST=Arizona,  
C=US  

## Quick Start

1. Create `secret/services.json` file.  
The example of the file is provided. **See** [services-example.json](secret/services.json).  
You can get `client_id` and `secret` in [Twitch Developer Console](https://dev.twitch.tv/) and [blizzard dev console](https://develop.battle.net/). **Note**, ***secret*** is required only for blizzard [Client Credential Flow](https://develop.battle.net/documentation/guides/using-oauth/client-credentials-flow) and ***MUST NOT*** be provided to any other party.  
You can get `token` for twitch setting up local [server](https://github.com/Roout/twitch-token) and opening it in browser at "http://localhost:3000". **See** more information at the page of [twitch-token generator](https://github.com/Roout/twitch-token).
2. Build Project with CMake 3.12 or higher.

## Application structure

...
