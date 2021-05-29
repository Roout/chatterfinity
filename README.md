# Chatterfinity

Chat bot intended to provide access to Wow database through chat in twitch.
Other functionality will be added later and is not planned at all yet.

The main target of this project is learning the proccess of building chat bots for
such platforms as Twitch and Discord (not sure if I will even try to work with discord).

Targets:

- Twitch
- Discord
- [WOW API](https://develop.battle.net/documentation/world-of-warcraft-classic/game-data-apis)
  - what creature are you? (random index used to genereate GET /data/wow/creature-family/${index})
  - check realm for queue
  - arena leaderboard
  - auction (don't know whether it will be usefull)?

## CA

[DigiCert list](https://www.digicert.com/kb/digicert-root-certificates.htm#roots)
Certificate Chain:
DigiCert High Assurance EV Root CA => DigiCert SHA2 High Assurance Server CA => *.battle.net
So root cert is DigiCert High Assurance EV Root CA;
Valid until: 10/Nov/2031
