# io-homecontrol : etude de faisabilite

Note d'exploration, pas un engagement. Objectif : evaluer si OpenProfalux pourrait un
jour piloter des volets et fenetres **io-homecontrol** (Velux, moteurs Somfy recents)
depuis la meme carte radio, sans ajouter de puce.

## Pourquoi

Plusieurs utilisateurs ont un foyer mixte : du Profalux ou du Somfy RTS, et **aussi** du
Velux ou du Somfy io. La carte recoit et emet deja sur 433 et 868 MHz. La question est
donc uniquement logicielle : OpenProfalux peut-il parler io ?

## Ce qu'est io-homecontrol (couche radio)

Parametres publics, documentes par la communaute et par le decodeur `rtl_433` :

| Point | Valeur |
|---|---|
| Bande | 868 MHz |
| Modulation | 2-FSK, deviation 19,2 kHz, codage NRZ |
| Debit | 38 400 bps (UART 8N1, ~26 us/bit) |
| Canaux | 868,25 / 868,95 / 869,85 MHz |
| Trame | preambule 256 bits, sync `0xFD99`, paquet, CRC |
| Paquet | entete, adresse, charge utile, code roulant, HMAC |
| Chiffrement | AES-128 |
| Sens | half-duplex, modes 1W (une voie) et 2W (bidirectionnel) |

## Difference avec Somfy RTS

Le Somfy RTS (deja integre, voir `IMPLEMENTATION-SOMFY-RTS.md`) est du **OOK en clair**,
simple a emettre. io-homecontrol est **FSK chiffre AES, bidirectionnel**, avec un
appairage a cle. C'est nettement plus lourd : ce n'est pas une variante du RTS.

Point important : la cle AES n'est **pas** une cle fabricant globale. Elle est propre a
chaque installation et s'etablit a l'appairage (acces physique). Le modele est donc un
**appairage**, pas une cle universelle.

## Faisabilite radio

La radio CC1101 de la carte fait du 2-FSK a 868 MHz avec debit et deviation reglables.
Les parametres ci-dessus sont dans sa plage (indice de modulation = 1,0). Cote radio,
c'est donc jouable. A valider au banc : rien ne remplace un essai sur un vrai appareil.

## Ce sur quoi on s'appuie

Le protocole a deja ete reverse par la communaute. OpenProfalux ne repart pas de zero et
ne redistribue aucun firmware proprietaire. Travaux publics de reference :

- **[Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol)** : documentation du protocole, decodeurs (`rtl_433`, GNU Radio), analyse de la couche liaison.
- **[rspaargaren/iown-home](https://github.com/rspaargaren/iown-home)** : implementation ESP32, 1W et 2W, annoncee fonctionnelle y compris sur Velux, sur radios SX127x / LoRa32.
- **[rtl_433](https://github.com/merbanan/rtl_433/blob/master/src/devices/somfy_iohc.c)** : decodeur io-homecontrol integre en amont.

Leurs implementations tournent sur SX127x. Le portage vers le CC1101 n'est pas encore
prouve par ces auteurs ; c'est justement la part qui reste.

## Ce qui reste

1. **Porter** la logique (PHY FSK, AES, appairage 1W/2W) sur le CC1101 de la carte.
2. **Valider sur un vrai appareil io** (Velux fenetre de toit ou store, ou moteur Somfy
   io) : captures, puis essais d'appairage et de commande.

## Statut

Exploration. Aucune date, aucune promesse de support io tant que ce n'est pas eprouve sur
un vrai moteur. L'appel a du materiel de test est suivi dans une issue dediee.
