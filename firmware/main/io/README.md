# Module io-homecontrol (en cours)

Couche protocole io-homecontrol reprise du projet **rspaargaren/iown-home**
(licence **CC0-1.0**, domaine public) : construction/lecture de trames, crypto
AES-128, machine d'etat 2W (decouverte, appairage, transfert de cle, defi-reponse),
constantes.

Fichiers repris tels quels (portables : mbedTLS, parties Arduino derriere `#ifdef ARDUINO`) :

- `iohome_frame.*` : trames (serialize/parse), appelle `crypto` pour HMAC et CRC.
- `iohome_crypto.*` : AES-128 (mbedTLS), HMAC, CRC16.
- `iohome_2w.*` : machine d'etat bidirectionnelle (2W).
- `iohome_constants.h`, `iown_mac.h`, `iown_frame.h` : definitions communes.

A ecrire (ne vient PAS d'iown-home, qui utilise un SX1276 via RadioLib) :

- backend **radio CC1101 en 2-FSK** (38 400 bps, deviation 19,2 kHz, sync 0xFD99,
  canaux 868,25 / 868,95 / 869,85), sauts de canal, TX/RX paquet ;
- integration dans le modele volet : dimension `VOLET_PROTO_IO` (comme Somfy RTS),
  mais bidirectionnelle (RX + TX) ;
- banc de test hote (frame + crypto), sur le patron de `firmware/test/keeloq`.

Statut : module pose, compile dans notre chaine. Radio FSK + integration + validation
sur un vrai appareil io (voir issue #87) a suivre. Voir `docs/IO-HOMECONTROL.md`.
