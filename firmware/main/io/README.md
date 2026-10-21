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

Backend radio CC1101 (`io_radio.c/.h`) : la config registres 2-FSK est reprise de
**samr037/iohc-flipper** (`radio_cc1101_regs.h`, licence **MIT**), qui pilote de l'io sur un
vrai CC1101. Valeurs prouvees : 2-FSK, ~38 383 bps, deviation ~19 kHz, sync **0x57FD** (queue
de preambule + 0xFF encode UART, pas 0xFD99), canaux 868,25 / 868,95 / 869,85 MHz, FIFO
longueur fixe. `io_radio_config()` et `io_radio_set_channel()` sont poses ; l'emission/reception
par la FIFO reste a cabler (le chemin OOK actuel passe par le RMT, pas la FIFO).

Reste a ecrire :

- primitives **FIFO/strobe** dans `cc1101.c` puis `io_radio_tx_packet` / `io_radio_rx_packet`
  (reference : iohc-flipper `radio_cc1101.c` / `phy_rx.c`, MIT) ;
- integration dans le modele volet : dimension `VOLET_PROTO_IO` (comme Somfy RTS),
  mais bidirectionnelle (RX + TX) ;
- banc de test hote (frame + crypto), sur le patron de `firmware/test/keeloq`.

Statut : module pose, compile dans notre chaine. Radio FSK + integration + validation
sur un vrai appareil io (voir issue #87) a suivre. Voir `docs/IO-HOMECONTROL.md`.
