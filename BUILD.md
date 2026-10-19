# Build & flash : OpenProfalux

## Choisir sa carte

Le brochage vient de `boards/<carte>.json`, lu à la configuration CMake. Une carte
inconnue fait échouer la configuration, elle ne compile pas avec de mauvaises broches.

| `-DBOARD=` | Carte | Cible |
|---|---|---|
| `isno-super` | ISNO Super (ESP32-S3-MINI-1-N8, CC1101 868 intégré) | `esp32s3` |
| `external` | ESP32-WROOM DevKit + CC1101 externe | `esp32` |
| `d1-mini` | D1 mini ESP32 + CC1101 externe (GDO0 sur IO26, SPI 1 MHz) | `esp32` |
| `m5-atom-lite` | M5Stack ATOM Lite + CC1101 en Dupont | `esp32` |
| `esp32-poe` | Olimex ESP32-POE + CC1101 externe (Ethernet LAN8720 en plus du Wi-Fi) | `esp32` |

## Compiler

ESP-IDF **v6.1** : depuis la migration du 2026-09-27, le dépôt ne compile plus en v5.

```bash
docker run --rm -v "$PWD":/project -w /project/firmware espressif/idf:v6.1 bash -c "
    idf.py -B build-isno-super set-target esp32s3
    idf.py -B build-isno-super -DBOARD=isno-super build
"
```

Un répertoire de build **par carte** : deux cibles différentes ne partagent pas le
même cache CMake. Remplacer les trois valeurs pour une autre carte.

Ou via docker compose, qui construit l'image depuis `Dockerfile.esp-idf` :

```bash
docker compose run --rm esp-idf bash -c "
    idf.py -B build-isno-super set-target esp32s3
    idf.py -B build-isno-super -DBOARD=isno-super build
"
```

La même chose tourne en intégration continue à chaque poussée, pour chaque carte
de `boards/` : voir `.github/workflows/firmware.yml`.

## Builds de test (version identifiable)

Une build de test doit s'annoncer avec une version **distincte de la release**, sinon le
testeur voit le même `0.2.11` que l'ancienne version et ne peut pas vérifier son flash.

`firmware/build-test.sh` s'en charge : il compile chaque carte de `boards/` avec une
version `<prochaine release>-<label>` (ex. `0.2.12-test9`), affichée au boot et dans
l'en-tête de l'UI.

```bash
./build-test.sh test9
```

Produit dans `firmware/release-test/` : `openprofalux-<carte>-test9.bin` (app seule, pour
l'onglet OTA) et `-test9-full.bin` (flash USB à 0x0). À publier en **pré-release** GitHub
(exclue de `/releases/latest`, donc pas d'OTA automatique proposée).

Sous le capot, la version vient de la variable d'environnement `OPF_VERSION` que
`CMakeLists.txt` lit si elle est définie ; `PROJECT_VER` (le numéro des releases) n'est
pas touché. Une release normale, elle, passe par `build-release.sh` et garde `PROJECT_VER`.

## Versions des composants

Les composants du registre (mdns, mqtt, cjson, lan87xx) sont figés dans un fichier
**commis** par puce : `firmware/dependencies.lock.esp32` et
`firmware/dependencies.lock.esp32s3`. Deux builds du même commit embarquent donc les
mêmes versions, comme la version d'ESP-IDF est figée par l'image `espressif/idf:v6.1`.

Monter une version se décide : effacer le fichier de la puce, puis
`idf.py -B build-<carte> reconfigure` sur une carte de cette puce (il est recalculé ;
un simple `build` ne le recrée pas), vérifier, commettre. `idf.py update-dependencies`
ne convient pas, il ne connaît que le nom par défaut `dependencies.lock`. La CI échoue si
un build a dû créer ou recalculer l'un de ces fichiers (manifeste `idf_component.yml`
modifié, nouvelle puce) : commettre celui qu'elle affiche.

## Flasher + console série

Décommenter la section `devices:` de `docker-compose.yml` et ajuster le port.

```bash
docker compose run --rm esp-idf bash -c "
    idf.py -B build-isno-super -p /dev/ttyUSB0 flash monitor
"
```

## Setup MQTT côté HA (pré-requis)

```yaml
# configuration.yaml (=ou via Mosquitto broker addon HA)
mqtt:
  broker: 192.168.1.x
  discovery: true
  discovery_prefix: homeassistant
```

## Config initiale ESP32 (=au premier boot)

1. ESP32 démarre → boot log verbose via UART
2. Aucun WiFi configuré → point d'accès **ouvert** `OpenProfalux_XXXX` (fin de la MAC)
3. Se connecter : le portail captif ouvre la page de configuration (sinon http://192.168.4.1)
4. Configurer :
   - onglet **Wi-Fi** : SSID + password, nom du boîtier (=ex `volet_chambre_invitee`, facultatif)
   - ESP32 reboot → connect WiFi, à l'adresse affichée (`http://<nom>.local`)
   - onglet **MQTT** : broker URI (=`mqtt://192.168.1.x:1883`)
5. ESP32 reboot → connect MQTT → HA discovery auto

## Debug depuis mon côté (=via MQTT)

```bash
# Subscribe à tous les logs et frames émises
mosquitto_sub -h 192.168.1.x -v -t 'openprofalux/#'

# Voir le state du boîtier (<nom> = nom du boîtier, ex. volet_test)
mosquitto_sub -h 192.168.1.x -t 'openprofalux/volet_test/state'

# Envoyer commande à un volet (<id> : voir les topics openprofalux/cover/+/state)
mosquitto_pub -h 192.168.1.x -t 'openprofalux/cover/<id>/set' -m 'OPEN'

# Capture RX pour analyse trames télécommande d'origine (écoute RF permanente)
mosquitto_pub -h 192.168.1.x -t 'openprofalux/listen/set' -m 'ON'
mosquitto_sub -h 192.168.1.x -t 'openprofalux/frames/#' -v
# ... presser bouton sur télécommande d'origine, voir les trames captured ici ...
mosquitto_pub -h 192.168.1.x -t 'openprofalux/listen/set' -m 'OFF'
```

## Logs disponibles

Le firmware publie sur MQTT :
- `openprofalux/log` : topics reçus et connexion (=`{"lvl":"info","msg":"...","client":"..."}`)
- `openprofalux/{device}/state` : state actuel (=dernière commande, rssi, heap)
- `openprofalux/{device}/status` : `online` / `offline` (disponibilité HA)
- `openprofalux/frames/last` et `openprofalux/frames/log/<n>` : trames RF captées (écoute activée)
- `openprofalux/system/status` : version, target, heap free (à la connexion)

Depuis mon côté (=Claude), je peux :
1. Te demander de lancer `mosquitto_sub -h {ip} -v -t 'openprofalux/#' > /tmp/log.txt`
2. Puis toi tu me colles le contenu via chat
3. Je peux analyser les frames, counters, résultats

Ou si le broker MQTT est exposé sur le LAN (=via WiFi routable depuis outside), je peux idéalement lancer moi-même mosquitto_sub via Bash.

## Simulation PC (=déjà testable maintenant)

```bash
cd sim
make
./sim_openprofalux       # Full simulation avec verbose logs
./test_keeloq          # Vérification algo KEELOQ
```
