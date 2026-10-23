# io-homecontrol : etude de faisabilite et avancement

Note d'exploration, pas un engagement. Objectif : evaluer si OpenProfalux peut piloter
des volets et fenetres **io-homecontrol** (Velux, moteurs Somfy recents) depuis la meme
carte radio, sans ajouter de puce. Depuis la premiere version de cette note, on a mesure
sur de vraies trames io : la section "Ce qu'on a mesure" fait foi sur les points confirmes.

## Pourquoi

Plusieurs utilisateurs ont un foyer mixte : du Profalux ou du Somfy RTS, et **aussi** du
Velux ou du Somfy io. La carte recoit et emet deja sur 433 et 868 MHz. La question est
donc uniquement logicielle : OpenProfalux peut-il parler io ?

## Ce qu'est io-homecontrol (couche radio)

Parametres publics, documentes par la communaute et par le decodeur `rtl_433` :

| Point | Valeur |
|---|---|
| Bande | 868 MHz |
| Modulation | 2-FSK, deviation ~19 kHz, codage NRZ |
| Debit | ~38 400 bps (UART 8N1, ~26 us/bit) |
| Canaux | 868,25 / 868,95 / 869,85 MHz |
| Trame | preambule, mot de synchro `0x57FD` (queue de preambule + 0xFF encode UART), paquet, CRC |
| Paquet | entete, adresse, charge utile, code roulant, HMAC |
| Chiffrement | AES-128 |
| Sens | half-duplex, modes 1W (une voie) et 2W (bidirectionnel) |

## Ce qu'on a mesure (sur de vraies trames Somfy io)

Captures reelles via un testeur (@lecanard38, issue #87), decodees par notre carte en
2-FSK. Une commande de volet Somfy io tient en **25 octets** :

| Champ | Taille | Valeur observee |
|---|---|---|
| octet de controle 0 | 1 | `0xF6` = debut+fin (0xC0) + bit 1W (0x20) + longueur (22) |
| octet de controle 1 | 1 | `0x00` (telecommande) ou `0x20` (basse consommation) |
| destination | 3 | `00 00 3F` (broadcast Somfy) |
| source | 3 | identite de l'emetteur |
| commande | 1 | `0x00` (appui bouton) |
| charge utile | 6 | `[origine 0x01, fabricant 0x43 (Somfy), bouton, 0x00, 0x00, 0x00]` |
| code roulant | 2 | anti-rejeu |
| HMAC | 6 | AES tronque |
| CRC | 2 | CRC-16/KERMIT (polynome 0x8408), octet faible d'abord |

Codes bouton mesures : monter `0x00`, descendre `0xC8`, stop `0xD2`.

Le HMAC 1W est un AES-128 (mode ECB) d'un IV construit (charge utile completee a `0x55`,
checksum, code roulant), tronque a 6 octets. Notre construction d'IV reproduit octet pour
octet l'exemple documente par Velocet. Le transfert de cle d'appairage (commande `0x30`)
enveloppe la cle par AES avec une cle de transfert publique, sur un IV egal a l'adresse
source repetee.

## Difference avec Somfy RTS

Le Somfy RTS (433 MHz, en cours d'integration, PR #75) est du **OOK en clair**,
simple a emettre. io-homecontrol est **FSK chiffre AES, bidirectionnel**, avec un
appairage a cle. C'est nettement plus lourd : ce n'est pas une variante du RTS.

Point important : la cle AES n'est **pas** une cle fabricant globale. Elle est propre a
chaque installation et s'etablit a l'appairage (acces physique). Le modele est donc un
**appairage**, pas une cle universelle.

## Faisabilite radio : la couche physique est portee sur le CC1101

Le CC1101 de la carte fait le 2-FSK io (configuration dediee : mot de synchro `0x57FD`,
canal CH2, lecture et ecriture par la FIFO). A ce jour, **la carte recoit et emet des
trames io bien formees** : un testeur a capture de vraies trames Somfy io avec elle
(reception), et elle emet des commandes a la meme forme que la vraie telecommande (octet
de controle, longueur, charge utile et CRC identiques, verifies contre deux captures
reelles). La couche physique et le format de trame ne sont donc plus la question.

## Ce sur quoi on s'appuie

Le protocole a deja ete reverse par la communaute. OpenProfalux ne repart pas de zero et
ne redistribue aucun firmware proprietaire. Travaux publics de reference :

- **[Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol)** : documentation du protocole, decodeurs (`rtl_433`, GNU Radio), couche liaison, exemples de crypto.
- **[rspaargaren/iohomecontrol](https://github.com/rspaargaren/iohomecontrol)** (et `iown-home`) : implementation ESP32 1W/2W, orientee **Velux** (controleur KLIxxx), sur radios SX127x.
- **[danielpetrovic/somfy-io-bridge](https://github.com/danielpetrovic/somfy-io-bridge)** : implementation **Somfy** io (ESPHome), appairage 1W.
- **[samr037/iohc-flipper](https://github.com/samr037/iohc-flipper)** : capture et re-emission **Somfy** io (Flipper), notes d'appairage.
- **[rtl_433](https://github.com/merbanan/rtl_433/blob/master/src/devices/somfy_iohc.c)** : decodeur io-homecontrol integre en amont.

Attention : ces projets ne ciblent pas tous la meme marque. L'octet fabricant et des
details d'appairage changent entre Velux et Somfy. Pour un moteur Somfy, les references
Somfy (danielpetrovic, samr037) priment sur les references Velux.

## Ce qui reste : l'appairage

Emettre une commande propre ne suffit pas : le moteur n'obeit qu'a un emetteur qu'il a
**enrole** (il stocke source + cle, puis verifie le HMAC avec cette cle). Tout se joue
donc a l'enrolement, et c'est la seule piece non prouvee :

- notre appairage 1W (poussee de cle `0x30`, rafale `0x20`, confirmation `0x2e`) reproduit
  celui de danielpetrovic, qui le marque lui-meme comme non prouve sur materiel reel ;
- samr037 indique que l'appairage io est **2W** (le moteur repond et echange la cle, avec
  saut de frequence), ce qu'un seul CC1101 a du mal a **recevoir** pendant le saut.

La question ouverte : l'enrolement Somfy io est-il faisable en 1W (pari non prouve), ou
impose-t-il un echange 2W ? La capture d'un enrolement reel sur un vrai moteur doit
trancher. C'est l'etape en cours.

## Statut

Couche physique et format de trame : **portes et verifies sur CC1101** contre de vraies
trames. Appairage : **en cours, non prouve**. Aucune date ni promesse de support io tant
que ce n'est pas eprouve sur un vrai moteur. Les essais sont suivis dans l'issue #87.
