# OpenProfalux

> Matériel testé : voir [docs/COMPATIBILITE.md](docs/COMPATIBILITE.md).


**Piloter ses volets roulants Profalux depuis Home Assistant avec un ESP32 à ~15 €, sans la clé constructeur.**

Firmware ESP32 + CC1101 open-source pour les volets **Profalux 868 MHz** (moteurs MAI-EMPX /
MAI-EMNOE). Tout reste local : pas de cloud, pas de passerelle propriétaire, aucun fil à tirer.

📖 **Montage pas à pas, photos et l'histoire complète du reverse** : [www.isno.fr/projets/openprofalux](https://www.isno.fr/projets/openprofalux)

🔌 **Sans rien câbler** : je fais fabriquer une carte qui intègre tout d'origine (ESP32-S3, radio 868 MHz, antenne SMA, USB-C, protections ESD) et qui tourne avec ce même firmware. Prototypes en fabrication, pas encore en vente : [liste d'attente sur isno.fr](https://www.isno.fr/liste-attente).

## Le principe : cloner une télécommande, pas casser la crypto

Les télécommandes Profalux sont en KeeLoq (rolling code), donc une trame est a priori
impossible à rejouer. J'ai passé des jours à chasser la clé constructeur... avant de tester la
chose la plus bête, celle que j'aurais dû essayer en premier :

> **Le moteur accepte une trame rejouée.** Le compteur anti-rejeu n'est **pas** vérifié par le
> récepteur : une trame capturée puis réémise fait bouger le volet, même après avoir utilisé la
> vraie télécommande entre-temps. Donc **aucune clé n'est nécessaire.**

OpenProfalux **clone** donc une télécommande, sans crypto : on capture les trames ▲ / ■ / ▼
d'une vraie télécommande, on les nomme, et on les rejoue à la demande depuis Home Assistant.
C'est tout. La clé constructeur, explorée longuement (cf. `firmware-capture-test/` et `docs/`),
s'est révélée inutile pour le pilotage.

## Ce que fait le firmware (`firmware/`)

- **Cover Home Assistant** par volet (MQTT discovery) : ouvrir / fermer / stop / position.
- **UI web embarquée** : pilotage, apprentissage, calibration, moniteur RF, config.
- **Apprentissage** : capture ▲/▼/■ d'une vraie télécommande, nommage du volet.
- **Position** estimée par le temps (calibration montée/descente, recalage aux butées).
- **Suivi de la vraie télécommande** : ▲/▼ lance le moteur, ■ fige (modèle appui + stop).
- **Multi-télécommandes** par volet.
- **Sauvegarde / restauration** de la config (noms + trames de référence + calibration).
- **OTA** : upload web + pull MQTT + rollback (2 partitions OTA).
- **Capture de trames** optionnelle vers MQTT (dédup par serial).

> **Portée pendant l'apprentissage.** La capture d'une télécommande et l'enrôlement d'un
> volet passent par la radio : garde la télécommande d'origine (et le volet) à portée du
> boîtier pendant ces étapes. Une fois le volet mémorisé, le pilotage se fait à distance.

→ Détails, build et flash : **[`firmware/README.md`](firmware/README.md)**.

## Matériel

| Composant | Réf | Coût |
|-----------|-----|------|
| ESP32 (M5Stack ATOM Lite ou DevKit) | ESP32-WROOM / PICO | ~5-12 € |
| Module CC1101 868 MHz | petit module vert 868 (pastilles au pas de 2 mm, pas le 2.54) | ~3 € |
| Antenne 868 MHz | fil 8.6 cm quart d'onde, ou hélicoïdale SMA | ~2 € |

Fréquence mesurée : **868.425 MHz** OOK (cf. `docs/`). **CC1101 = 3.3 V max, jamais 5 V.**

## Cartes et construction

Le brochage ne vit pas dans le code : chaque carte est déclarée dans
`boards/<carte>.json`, et `tools/gen_board.py` en engendre `board_pins.h` à la
configuration CMake (rien d'engendré n'est commis). Le firmware **lit** ces
broches, il n'en connaît aucune. Ajouter une carte, c'est ajouter un
`boards/<nom>.json`.

| `-DBOARD=` | Carte | Cible IDF |
|---|---|---|
| `isno-super` | ISNO Super (ESP32-S3-MINI-1-N8, CC1101 868 intégré) | `esp32s3` |
| `external` | ESP32-WROOM DevKit + CC1101 externe | `esp32` |
| `m5-atom-lite` | M5Stack ATOM Lite + CC1101 en Dupont | `esp32` |

```bash
cd firmware
idf.py -B build -DSDKCONFIG=build/sdkconfig -DIDF_TARGET=esp32s3 -DBOARD=isno-super build
```

`isno-super.json` est une **copie** de la source de vérité
`isno-launcher/boards/isno-super.json` : une copie inévitable se vérifie, elle
ne se tient pas à la main.

## Structure du dépôt

| Dossier | Rôle |
|---------|------|
| `firmware/` | **Firmware produit** : cover HA, UI, clone/replay, OTA, sauvegarde |
| `firmware-capture-test/` | Firmware de **banc / reverse** : capture RF, tests KeeLoq, mesures |
| `docs/` | Architecture, mesures RF, synthèses de la recherche |
| `sim/` | Harness de vérification KeeLoq (cipher + codec) côté PC |

## Statut

- ✅ Firmware produit **compile** (ESP-IDF v6.1, 1,29 Mo pour `isno-super`, 14 % libres, partitions OTA).
- ✅ **Rejeu validé au banc** (le moteur suit une trame rejouée, y compris après la vraie télécommande).
- ⏳ À valider sur l'installation : calibration des temps de course, portée antenne, multi-volets.

## Extensions possibles

Même approche (capture + rejeu) potentiellement applicable à d'autres volets 868 MHz à
rolling code non contraint (Delta Dore X2D, France Fermetures LIBRIO…), non vérifié.

## Remerciements

Merci aux personnes qui font avancer le projet :

- [@Akkeoss](https://github.com/Akkeoss) : authentification de l'interface web, ordre des volets dans l'interface, la liste des membres d'une centrale portée sur le tas (fin des troncatures au-delà de 384 octets), la mise à jour depuis GitHub rétablie (55 Ko de RAM rendus pour la poignée de main TLS), et la garde de version du script de release.
