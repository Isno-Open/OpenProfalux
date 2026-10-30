# OpenProfalux : Architecture & conception

> Document de référence. Modèle **clone/replay** validé au banc (2026-08-14).
> Il **remplace** les hypothèses antérieures (récupération de clé, enrôlement) :
> voir §9 pour pourquoi elles sont abandonnées.

## 1. Objectif

Piloter des volets roulants **Profalux 868 MHz** (moteurs MAI-EMPX / MAI-EMNOE, télécommande Noé,
famille KeeLoq `0x013`) depuis Home Assistant, via un **ESP32 + CC1101**, **sans clé cryptographique**
et sans sacrifier de matériel. Objectif secondaire : suivi de position (%), robuste au mélange
commandes HA + télécommande physique.

## 2. Le fait fondateur : le replay suffit (prouvé)

Les moteurs Profalux visés **n'appliquent aucun contrôle de code tournant** côté récepteur.

- **Niveau 1** (rejeu répété d'une même trame) : ✅ prouvé : une trame capturée rejouée X fois
  actionne le volet.
- **Niveau 2** (robustesse) : ✅ prouvé : en **alternant** vraie télécommande et rejeu, l'ancienne
  trame capturée **continue de marcher**. Donc le compteur n'est jamais vérifié.

**Conséquence** : on capture **une** trame par bouton (UP / DOWN / STOP), on la **rejoue à vie**.
Pas de clé, pas de compteur, pas de génération de trame. C'est pourquoi la crypto KeeLoq (§9) est
**inutile pour l'objectif**.

Raison technique : en usage normal, maintenir un bouton = ré-émettre **la même** trame (même hopping,
bit RPT à 1 sur les répétitions). Le moteur **doit** donc accepter les répétitions d'un hopping
identique, sinon un maintien ne fonctionnerait pas. Le rejeu exploite exactement ce comportement.

> Constat sécurité : ces HCS300 (génération ~2009) sont **totalement rejouables** côté RX.
> Faiblesse réelle du récepteur, mais c'est précisément ce qui rend OpenProfalux trivial.

## 3. Comment fonctionne la RF Profalux (référence)

- **PHY** : OOK 868,35 MHz, PWM type HCS300/301, TE ≈ 450 µs, préambule ~23×TE, header, guard.
- **Codeword HCS300, 66 bits**, ordre d'émission LSB-first par champ :
  `[hopping 32b] [serial 28b] [button 4b] [VLOW] [RPT]`
- **Chiffre** : KeeLoq **Microchip standard** (NLF `0x3A5C742E`, 528 tours). Confirmé 2000/2000 :
  notre `kloop` = decrypt standard, `kloop_encrypt` = encrypt standard.
- **Plaintext chiffré (32b)** : `button(4) | OVR(2) | disc(10) | counter(16)`, avec
  `disc = serial & 0x3ff`. Pour nos moteurs : **serial `0x0000813` → disc `0x013`**.
- **Le bouton est doublé** : en clair (champ 4b) **et** chiffré dans le hopping. Un hopping est donc
  lié à SON bouton → une trame capturée par commande (UP/DOWN/STOP).

> ⚠️ Ordre des bits : le hopping se lit **LSB-first** (aligné sur le serial qui donne 0x813).
> Un décodeur qui sort le hopping en MSB donne des trames à l'envers (bug historique corrigé).

## 4. Architecture système

```
        868 MHz OOK
Télécommandes ───────────►┐
(Noé, murales)            │  SNIFF (RX)          ┌──────────────┐
                          ▼                      │ Home Assistant│
                   ┌─────────────┐   MQTT/HA     │  cover.*      │
                   │ ESP32+CC1101│◄─────────────►│  (position %) │
                   │ OpenProfalux│               └──────────────┘
                   └─────────────┘
                          │  REPLAY (TX) des 3 hoppings figés
                          ▼
                    Moteur volet
```

Deux flux **distincts** :

- **Contrôle (TX)** = rejeu des 3 hoppings figés (UP/DOWN/STOP) capturés une fois. Suffit (§2).
- **Position (RX)** = sniff continu de **toutes** les télécommandes du volet pour ne jamais
  désynchroniser la position quand quelqu'un utilise une commande physique.

## 5. Suivi de position (sans capteur)

Le moteur ne renvoie **aucune** position (télécommande HCS300 = TX-only). La position est donc
**toujours estimée**, jamais mesurée, exactement comme la box GW le fait en interne.

- **Time-based** : mesurer une fois le temps de course complète (haut→bas). Intégrer le temps de
  marche depuis une référence.
- **Recalage aux butées** : à chaque course pleine (0% ou 100%), remettre la position à la référence.
  Corrige toute dérive.
- **Un seul modèle**, nourri par **deux sources** : les commandes HA (nos rejeux) ET les commandes
  sniffées des télécommandes physiques. Aucune désync possible.
- **Position préférée** (si la Noé a le bouton « my position ») : on rejoue cette trame → le moteur
  se positionne lui-même. Plus précis que le time-based, gratuit.

## 6. Multi-serial par volet (calibration par découverte)

Un volet peut être appairé à **plusieurs** télécommandes de **serials différents** (murale `0x813`
+ canaux de la Noé, potentiellement d'autres serials). Le moteur obéit à toutes.

- **Contrôler** = cloner **une seule** de ces télécommandes (rejeu).
- **Suivre la position** = sniffer **toutes** ses télécommandes.

**Wizard de calibration** (découverte, ne rien deviner) : par volet, l'utilisateur actionne
**toutes** les télécommandes qui le bougent, confirme « c'est le volet A » ; l'ESP32 enregistre
`{tous les serials vus} → volet A`. Le code est agnostique 1 ou N serials.

## 7. Firmware (fork du squelette OpenXtraflame)

OpenProfalux **réutilise** l'ossature d'OpenXtraflame (ESP-IDF C, UI single-file, plomberie).

| Module | Rôle | Source |
|---|---|---|
| `main.c`, `config_nvs`, `wifi_bridge`, `mqtt_bridge`, `ota`, `web_ui`, `log_ring` | plomberie | **repris** OpenXtraflame |
| `cc1101.c` | driver RF 868 OOK (RadioLib ou natif) | à finaliser sur le vrai module |
| `keeloq.c` | codec KeeLoq std (encode/decode) | **présent** |
| `profalux.c` | codeword HCS300 : parse (sniff) + build (replay) + PWM | présent, à aligner |
| `shutters.c` | cover : replay 3 hoppings + position time-based + calibration | **à écrire** |
| `frame_log.c` | log MQTT dédupliqué + compteur dataset slide | **à écrire** |

Conventions (comme OpenXtraflame) : fichiers ≤ 150-200 lignes, CSS externe (pas inline), thème.

## 8. UI web (calquée OpenXtraflame)

Single-file `web/index.html` + `style.css`, vanilla JS (tabs `data-tab`, `fetch /api/*.json`).

```
📊 Dashboard    : volets, position live, up/down/stop/%
🪟 Volets       : config par volet, mapping serial→volet
📱 Télécommandes : serials détectés (temps réel)
🎯 Calibration  : wizard (découverte multi-serial + temps de course)  [NEUF]
📻 RF Monitor   : trames live (serial/button/hopping/rssi) = mini-Flipper  [NEUF]
💾 Dataset      : progression vers 65536 par serial + « lancer slide »  [NEUF]
📶 Wi-Fi · 📡 MQTT · ⬆️ OTA : repris tels quels
```

## 9. Ce qui est ÉCARTÉ (et pourquoi) : post-mortem honnête

Une journée entière de crypto a mené à la conclusion que **rien de tout ça n'est nécessaire** :

- **Récupérer la clé device 0x813** : impossible en software. Mesuré : mur SAT ~96 tours (Cadical),
  portée différentielle ~120 tours, il faut **~48 bits de clé fixés** pour résoudre les 528 tours
  (aucun disponible). Seules voies réelles : **slide** (2¹⁶ captures + GPU ~3h) ou **DPA** (physique).
- **Enrôler une nouvelle télécommande** : **mauvaise piste.** Nos trames relèvent d'une famille
  KeeLoq (`0x013`) que les clés explorées côté crypto ne déchiffrent pas : **mauvaise famille, pas
  juste mauvaise clé.** L'enrôlement n'apporte rien pour l'objectif.
- **La procédure de la passerelle** (UI) = « appuyez sur votre télécommande d'origine » = elle
  **clone** l'originale (capture + rejeu). **Même mécanisme qu'OpenProfalux.** Elle n'enrôle pas.

**Outils crypto conservés en réserve** (au cas où un jour un moteur *strict* apparaîtrait) :
slide-MITM et DPA/CPA, validés hors ligne, gardés en recherche interne.

## 10. Dataset slide passif (optionnel, gratuit)

L'ESP32 sniffe déjà → publier chaque trame **distincte** en MQTT (dédupliquée) :
`profalux/frames/<serial>` = `{button, hop(LSB), rssi, ts, seq}` ; compteur
`profalux/stats/<serial>/distinct` ; flag `slide_ready` à 65536.
Multi-utile (audit, détection nouveau serial). Caveat honnête : remplissage passif à usage normal
≈ **60 ans** ; ce n'est une vraie piste que via un jig d'appui (36 h → 65536) si jamais besoin.

## 11. État de l'art / on ne réinvente pas

- **l0ad/profalux2Esphome** : Profalux→HA, mais **câblage physique** (soude une télécommande),
  **sans position**, et suppose (à tort) le rejeu impossible « rolling codes ». On fait mieux : RF pur.
- **dewenni/ESP32-Jarolift-Controller** : archi RF KeeLoq + position + HA (autre marque) → pattern
  cover à reprendre.
- **HarmEllis/esphome-cc1101**, **juanboro/esphome-radiolib-cc1101** : driver CC1101 → réutiliser.
- **intégration HA « thin » de la passerelle** : envoie % à la box, lit position → confirme
  que l'intelligence position vit dans l'appareil (chez nous : l'ESP32).

## 12. Matériel

- ESP32 (WROOM ou S3) + module **CC1101 868 MHz**.
- Antenne 868. Alim USB.
- (Aucune télécommande à sacrifier, contrairement à l0ad.)

## 13. Reste à faire (dépend du hardware)

- Finaliser `cc1101.c` sur le module réel (format de capture RAW / durées PWM).
- Écrire `shutters.c` (replay + cover time-based + calibration) et `frame_log.c`.
- 3 onglets UI neufs (Calibration, RF Monitor, Dataset).
- Vérifs banc restantes : table `bouton → action` (rejouer chaque trame, noter la direction) ;
  comportement RPT en rejeu de maintien.
