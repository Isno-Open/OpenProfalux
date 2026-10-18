# OpenProfalux — firmware (ESP32 + CC1101)

Pilote des volets roulants **Profalux 868 MHz** (moteurs MAI-EMPX / MAI-EMNOE) depuis
Home Assistant, **sans la clé constructeur** : le firmware **clone** une télécommande
existante (capture + rejeu) — le moteur accepte le rejeu (rolling code non contraint).

## Fonctions
- **UI web embarquée** : pilotage, apprentissage, calibration, Wi-Fi, MQTT, OTA, système (RF debug).
- **Cover Home Assistant** par volet (MQTT discovery) : open/close/stop + position.
- **Apprentissage** : on capture ▲/▼/■ d'une vraie télécommande et on nomme le volet.
- **Position estimée par le temps** (calibration montée/descente ; recalage aux butées).
- **Suivi de la vraie télécommande** : ▲/▼ lance, ■ fige (modèle appui + stop).
- **Multi-télécommandes** par volet (serials multiples).
- **Sauvegarde / restauration** : export JSON (noms + trames de référence + calibration).
- **Capture de trames** optionnelle vers MQTT (dédup par serial).
- **OTA** : upload web + pull MQTT + rollback (2 partitions OTA).
- **Config UI** : Wi-Fi + MQTT (persistés en NVS).

## Matériel
Une carte ESP32 (M5Stack ATOM Lite, DevKit, D1 mini, Olimex ESP32-POE, ISNO Super : tableau dans
[`BUILD.md`](../BUILD.md)) + module CC1101 868 MHz (EBYTE E07-900M10S).
Brochage : voir `boards/<carte>.json`. **CC1101 = 3.3 V max, jamais 5 V.**

## Build
```bash
# via Docker ESP-IDF v6.1 (un repertoire de build par carte)
docker run --rm -v "$PWD/..":/project -w /project/firmware espressif/idf:v6.1 bash -c "
  idf.py -B build-isno-super set-target esp32s3
  idf.py -B build-isno-super -DBOARD=isno-super build"
# autres cartes : -DBOARD=<fichier de boards/>, cible = son champ mcu (tableau dans BUILD.md)
# ou en local si ESP-IDF v6.1 est installe :
idf.py -B build-isno-super set-target esp32s3 && idf.py -B build-isno-super -DBOARD=isno-super build
```

## Flash

### Option A — release pré-compilée (le plus simple, pas d'ESP-IDF)
Télécharger les binaires de la [dernière release](https://github.com/Isno-Open/OpenProfalux/releases)
puis, avec juste `esptool` (`pip install esptool`). **Choisir le binaire selon la carte** — les
broches SPI diffèrent, un binaire d'une carte ne marchera pas sur l'autre :

| Carte | Board neuf (`write_flash 0x0`) | Mise à jour OTA (onglet OTA) |
|---|---|---|
| **M5Stack ATOM Lite** (ESP32-PICO) | `openprofalux-m5-atom-lite-full.bin` | `openprofalux-m5-atom-lite-ota.bin` |
| **ESP32 DevKit** (CC1101 externe) | `openprofalux-external-full.bin` | `openprofalux-external-ota.bin` |
| **D1 mini ESP32** (CC1101 externe) | `openprofalux-d1-mini-full.bin` | `openprofalux-d1-mini-ota.bin` |
| **Olimex ESP32-POE** (Ethernet) | `openprofalux-esp32-poe-full.bin` | `openprofalux-esp32-poe-ota.bin` |
| **ISNO Super** (ESP32-S3, `--chip esp32s3`) | `openprofalux-isno-super-full.bin` | `openprofalux-isno-super-ota.bin` |

> **Le `-full.bin` efface la configuration** (Wi-Fi, MQTT, volets, télécommandes clonées) : sa zone
> de configuration est vide. Sur un boîtier déjà configuré, exporter d'abord la sauvegarde (onglet
> **Système** → Sauvegarde), ou passer par l'OTA, qui la garde.

```bash
# Board neuf / réinstallation complète : image fusionnée à flasher à 0x0
esptool.py --chip esp32 -p /dev/ttyUSB0 --baud 460800 write_flash 0x0 openprofalux-m5-atom-lite-full.bin
```
Windows (PowerShell) : `esptool.exe --chip esp32 --port COM4 --baud 115200 write_flash 0x0 openprofalux-m5-atom-lite-full.bin`

> `460800` accélère l'écriture mais ne passe pas sur tous les câbles/puces USB. En cas d'erreur
> (*Timed out waiting for packet*, *invalid head of packet*), retomber à `--baud 115200`, qui marche partout.

Déjà sous OpenProfalux ? Pas besoin de câble : **onglet OTA → uploader le `-ota.bin`** de ta carte,
ou pousser son URL via MQTT (`openprofalux/ota/pull`).

### Option B — depuis les sources
```bash
# le repertoire de build de SA carte (voir Build) : sans -B, idf.py prend build/, isno-super par defaut
idf.py -B build-m5-atom-lite -p /dev/ttyUSB0 flash monitor
```

## Premier démarrage
1. Pas de Wi-Fi configuré → l'ESP32 ouvre un point d'accès **ouvert** `OpenProfalux_XXXX`
   (fin de son adresse MAC).
2. S'y connecter : le téléphone ouvre tout seul la page de configuration (portail captif), sinon
   ouvrir http://192.168.4.1. Onglet **Wi-Fi** : réseau, mot de passe, nom du boîtier (facultatif).
3. Redémarrage → le boîtier rejoint le réseau, à l'adresse affichée par la page
   (`http://openprofalux-xxxx.local`, ou `http://<nom>.local`).
4. Onglet **MQTT** : adresse du broker → les covers apparaissent dans Home Assistant.
5. Onglet **Télécommandes** : « Cloner une télécommande » (capturer ▲/▼/■, nommer le volet),
   puis « Calibrer la position » (chronométrer une course complète pour la position en %).

## Topics MQTT
| Topic | Sens | Payload |
|---|---|---|
| `homeassistant/cover/openprofalux_<dev>_<id>/config` | pub retain | discovery HA |
| `openprofalux/cover/<id>/set` | sub | `OPEN`\|`CLOSE`\|`STOP` |
| `openprofalux/cover/<id>/set_position` | sub | `0..100` |
| `openprofalux/cover/<id>/position` / `/state` | pub retain | position / état |
| `openprofalux/<nom>/status` | pub retain | `online` / `offline` (disponibilité HA) |
| `openprofalux/listen/set` | sub | `ON`\|`OFF` : écoute RF permanente (aussi dans **Système**) |
| `openprofalux/frames/last`, `openprofalux/frames/log/<n>` | pub retain | trames captées (si écoute activée) |
| `openprofalux/ota/pull` | sub | URL du `.bin` |

> Le firmware de reverse/banc (capture, tests KeeLoq) est dans `../firmware-capture-test/`.
