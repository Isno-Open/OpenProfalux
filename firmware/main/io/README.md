# Module io-homecontrol (1W)

Couche protocole reprise du projet **rspaargaren/iown-home** (licence **CC0-1.0**, domaine
public) : construction/lecture de trames, crypto AES-128, HMAC, CRC16, constantes. La config
registres CC1101 2-FSK est reprise de **samr037/iohc-flipper** (`radio_cc1101_regs.h`, licence
**MIT**) : ~38 383 bps, deviation ~19 kHz, sync **0x57FD**, canaux 868,25 / 868,95 / 869,85 MHz,
FIFO longueur fixe.

- `iohome_frame.*` : trames (serialize/parse), appelle `crypto` pour HMAC et CRC.
- `iohome_crypto.*` : AES-128, HMAC, CRC16.
- `iohome_constants.h` : definitions communes.
- `io_framer.*` / `io_unframer.*` : encapsulation UART 8N1 des octets io dans la FIFO.
- `io_radio.*` : config CC1101 io, emission/reception par la FIFO. Preambule de reveil de 500 ms
  sur la 1re trame d'une rafale (le recepteur d'un moteur solaire dort), 10 ms ensuite.
- `io_send.*` : un appui bouton 1W (commande 0x00), calque sur une trame captee de la vraie
  telecommande.
- `io_pair.*` : profil d'emission, identite derivee de la MAC, transfert de cle 0x30.
- `io_store.*` : cle, profil et compteur roulant par source (NVS, namespace `iohc`).

## Appairer un volet io

Depuis l'interface : Appareils, Ajouter un appareil, profil « Nouveau volet Velux io » (`velux-io-enrol` dans
`web/profiles.json`). C'est un ENROLEMENT : le boitier devient une nouvelle telecommande, on ne copie pas les touches.

1. `POST /api/io/sniff` : capte UNE commande (0x00) de la telecommande d'origine (8 s au plus). Rend la trame en hexa
   et `usable` (commande complete, CRC juste) : le boitier y lit la destination et la charge utile du volet.
2. Mettre le moteur en apprentissage avec la telecommande d'origine (Velux : appui long sur RESET, jusqu'au va-et-vient).
3. `POST /api/io/pair/1w` `{profile, frame, name}` : la carte envoie une seule rafale 0x30 (4 copies identiques) et cree
   le volet. Le code fabricant (`io.man_id`, 1 pour un Velux, seul accepte) et la marque viennent du PROFIL.

La source et la cle sont derivees de la MAC et d'un index, comme sur `feat/io-pair-known-identity` : meme carte + meme
index = meme identite. Sans `index`, le boitier prend le premier dont l'identite n'est celle d'aucun de ses volets io :
deux volets n'ont jamais la meme. La radio est celle de la carte dont la bande declaree couvre 868,95 MHz ; sans elle,
l'appairage est refuse.

**Reprise sans emission** (`"tx":0,"rolling":N,"index":I`) : apres un reflash ou un effacement NVS, recree la cle, le
profil et le volet sans rien emettre. `N` = prochain compteur a emettre, plus grand que le dernier accepte par le moteur.
Une valeur inferieure au compteur deja stocke est refusee ; le compteur ne revient jamais a zero (les commandes
s'arretent apres 65535).

La sauvegarde JSON de la configuration n'exporte pas les cles ni les compteurs io : noter le compteur avant d'effacer
la NVS.

## Commandes et position

Une commande = une rafale de 4 trames au meme compteur, reserve en NVS avant l'emission : bit LPM
(0x20) sur la 1re seulement, CRC recalcule pour les suivantes, comme la vraie telecommande. Pas de
nouvel essai automatique (batterie des moteurs solaires). Les volets io passent par la decouverte
HA et le suivi de position par temps de course communs ; une position intermediaire ajoute une
rafale STOP. Leur compteur vit dans `io_store`, hors des topics partages `counter/<serial>` et
`busy/<serial>` ; l'interrupteur « obeir aux ordres MQTT » s'applique. En 1W le moteur ne repond
rien : la position est estimee, et les appuis sur la telecommande d'origine ne sont pas suivis.

## Correction de frequence

Reglage `io_freq_offset_khz` (`/api/config`, et dans l'ecran d'ajout d'un volet io, « Rien ne passe ? ») : entier de -100 a 100,
0 par defaut, NVS `cfg/io_offset`. Corrige le quartz du module sur les seuls canaux io ; la
porteuse OOK n'est pas touchee. Un module M5Stack decale de -58 kHz (868 345 au lieu de 868 403
en Profalux) ne recoit ni n'est recu en io sans `-58`.

Valide sur un volet roulant solaire Velux (telecommande 3UR B01, type 860963). Banc hote :
`make -C firmware/test/io-1w test`. Voir `docs/IO-HOMECONTROL.md` et l'issue #87.
