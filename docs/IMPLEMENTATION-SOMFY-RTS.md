# Implémentation Somfy RTS dans OpenProfalux

But : piloter des volets **Somfy RTS** (433,42 MHz) en plus du Profalux (868 MHz),
idéalement depuis une seule carte (issue #73). Ce document décrit ce qui existe, ce
qu'il reste à faire, et dans quel ordre. Il sert de plan avant de coder la partie radio.

## 1. Le protocole en bref

- **Radio** : 433,42 MHz, modulation **OOK**, codage **Manchester** (convention G.E.
  Thomas : bit 1 = bas puis haut).
- **Trame** : 56 bits (7 octets). Octet 0 = clé (`0xA0 | quartet roulant`), octet 1 =
  commande (quartet haut) et checksum (quartet bas), octets 2-3 = code roulant 16 bits
  **big-endian**, octets 4-6 = adresse 24 bits **little-endian** (octet 4 = poids faible).
- **Checksum** : XOR de tous les quartets, le quartet de checksum mis à zéro pendant le
  calcul.
- **Obfuscation** : XOR en cascade vers l'avant à l'émission (`octet[i] ^= octet[i-1]`,
  i de 1 à 6), inverse à la réception.
- **Commandes** (quartet) : MY/stop=0x1, UP=0x2, DOWN=0x4, PROG=0x8 (appairage), etc.
- **Timings d'émission** (microsecondes, camp Pushstack + rtl_433, cf `somfy_rts.h`) :
  wakeup 9415/89565, sync matériel 2416 (demi-période, 2 paires à la 1re trame, 7 aux
  répétitions), sync logiciel 4550, symbole Manchester 1208, demi-symbole 604, silence
  inter-trame 30415.

⚠ **Point de vigilance adresse** : open-rts et rtl_433 lisent l'octet 4 en poids faible
(convention retenue). Nickduino et ESPSomfy-RTS le lisent en poids fort. Un vecteur
emprunté à l'autre camp inverse l'adresse en silence.

## 2. Ce qui existe déjà (PR #75)

Le **codec logique** est écrit et testé, dans `firmware/main/somfy_rts.c` / `.h` :

- `somfy_rts_checksum`, `somfy_rts_pack`, `somfy_rts_unpack` (avec checksum et
  dé-obfuscation), `somfy_rts_manchester` (trame -> suite de symboles).
- Banc hôte `firmware/test/somfy-rts/` : vérifié **bit à bit contre deux vecteurs réels
  d'open-rts** (loopj/open-rts), dans les deux sens (pack reproduit le fil, unpack rend
  les champs). Tourne sous `gcc` + sanitizers.

C'est une **unité pure**, sans radio ni ESP-IDF. Elle ne sait pas encore émettre ni
recevoir sur une vraie puce.

## 3. Ce qu'il reste à implémenter

### 3.1 La radio 433,42 MHz (le point dur)

Profalux est à **868**, Somfy à **433**. Une seule CC1101 sait se re-régler entre les
deux bandes, **mais le frontend RF (balun, antenne, matching) est spécifique à une
bande** : un module CC1101 868 ne rayonne pas correctement à 433, et inversement.

Trois options, selon la carte :

1. **Carte 433 dédiée** (ex. ESP32 + CC1101 433 de Machin77) : Somfy seul, simple.
2. **Deux modules radio** (un 433, un 868) : Profalux + Somfy en parallèle.
3. **ISNO Super** : son **diplexeur LC 433+868** sur antenne unique permet une seule
   radio re-réglable qui couvre les deux bandes. C'est la voie « une seule carte » de #73.

Décision à acter avant de coder : pour la cible « une seule carte », viser le chemin
Super (diplexeur). Pour un premier essai terrain, une carte 433 dédiée suffit.

### 3.2 L'émission

Construire la forme d'onde complète à partir de la sortie du codec :

1. **Wakeup** : un haut 9415 µs, un bas 89565 µs.
2. **Sync matériel** : paires haut/bas à 2416 µs (2 paires 1re trame, 7 aux répétitions).
3. **Sync logiciel** : 4550 µs.
4. **Données** : les 56 bits en Manchester (symbole 1208, demi-symbole 604), via
   `somfy_rts_manchester`.
5. **Silence inter-trame** 30415 µs, puis répétitions (la 1re trame porte 2 paires de
   sync, les répétitions 7).

À générer par le RMT (timing matériel), comme l'émission Profalux. Piloter OOK sur le
CC1101 réglé à 433,42 MHz.

### 3.3 La réception / capture

Pour **cloner une vraie télécommande Somfy** (capturer l'adresse + le code roulant) :
démoduler l'OOK, décoder le Manchester, dé-obfusquer, vérifier le checksum
(`somfy_rts_unpack`). Même chemin de capture que Profalux (GDO0 + RMT), mais puce à 433
et décodage Manchester au lieu de KeeLoq.

Utile aussi pour lire le code roulant courant d'une télécommande existante avant de
prendre le relais.

### 3.4 Le code roulant

Compteur **16 bits par télécommande virtuelle**, persisté en NVS, **incrémenté à chaque
commande émise**. Le moteur accepte une fenêtre de codes en avance ; il faut rester dans
cette fenêtre (ne pas sauter trop loin). À l'appairage, on part d'un compteur et le moteur
s'y cale.

### 3.5 La télécommande virtuelle Somfy

Une **adresse 24 bits** générée par OpenProfalux (comme on génère une identité Profalux),
plus le compteur. L'UI crée une « télécommande virtuelle Somfy » et on l'appaire au moteur.

### 3.6 L'appairage (PROG)

Procédure Somfy standard : mettre le **moteur en mode apprentissage** (maintenir PROG sur
une télécommande déjà appairée, ou la procédure d'usine), puis **émettre une commande PROG**
(0x8) depuis OpenProfalux avec l'adresse de la télécommande virtuelle. Le moteur fait un
va-et-vient de confirmation. Ensuite les commandes UP/DOWN/MY fonctionnent.

### 3.7 Intégration dans le firmware

- **Dimension protocole** (#76) : `volet_proto_t` existe déjà, Profalux vaut 0 ; ajouter
  une valeur Somfy RTS. `emit_virt` aiguille déjà sur `proto` ; brancher le chemin Somfy
  à côté du chemin Profalux (`emit_virt_profalux`).
- **Arbitrage radio** : réutiliser le mutex RX/TX de `radio.c`. Si la même puce sert 433
  et 868 (Super), prévoir le re-réglage de fréquence sous mutex avant/après émission.
- **UI** : choix du protocole à la création du volet virtuel, bouton PROG/appairage,
  gestion du code roulant.

## 4. La carte

- **Somfy seul** : ESP32 + **CC1101 433 MHz** (antenne/matching 433). C'est le matériel de
  Machin77 (#73 : ESP32-WROOM + CC1101 433).
- **Profalux + Somfy sur une carte** : **ISNO Super** (diplexeur 433+868), ou deux modules.

## 5. Plan de test

- **Codec** : fait, vecteurs open-rts (PR #75).
- **Banc d'émission** : vérifier la forme d'onde générée (durées, Manchester) contre un
  décodeur de référence (rtl_433, open-rts) ou une capture RTL-SDR.
- **Matériel réel** : Machin77 a des volets Somfy RTS + un CC1101 433. Dès qu'une build
  capture/émet du Somfy, lui faire tester la **capture** d'une vraie télécommande puis
  l'**appairage** d'une virtuelle.
- **Étape finale** : UP/DOWN/MY et PROG validés sur un vrai moteur Somfy.

## 6. Questions ouvertes et risques

- **Polarité Manchester à l'antenne** : la convention logique (bit 1 = bas puis haut) est
  celle des références ; reste à confirmer qu'il n'y a pas d'inversion entre le niveau
  logique et le signal rayonné (à valider au banc, cf `somfy_rts.h`).
- **Endianness de l'adresse** : convention open-rts/rtl_433 retenue ; une capture
  comparée à ESPSomfy/Nickduino doit être lue dans la bonne convention.
- **Partage 433/868** sur une seule puce : coût du re-réglage, et surtout antenne/matching
  (d'où le diplexeur Super).
- **Fenêtre de code roulant** : ne pas désynchroniser le compteur du moteur.
- **Timings wakeup et inter-trame** : divergent entre sources (Pushstack vs Nickduino),
  restent à valider au banc (issue #73).

## 7. Ordre proposé

1. Décider la cible matérielle du premier essai (carte 433 dédiée recommandée pour
   commencer, puis Super pour la carte unique).
2. Émission : générer la forme d'onde RMT à 433 et la vérifier au banc / RTL-SDR.
3. Appairage PROG sur un vrai moteur (Machin77), puis UP/DOWN/MY.
4. Réception/capture d'une vraie télécommande Somfy (clonage).
5. Intégration UI + code roulant persistant + dimension protocole (#76).
6. Carte unique Profalux + Somfy via le diplexeur Super.
