# Build & flash — OpenProfalux

## Choisir sa carte

Le brochage vient de `boards/<carte>.json`, lu à la configuration CMake. Une carte
inconnue fait échouer la configuration, elle ne compile pas avec de mauvaises broches.

| `-DBOARD=` | Carte | Cible |
|---|---|---|
| `isno-super` | ISNO Super (ESP32-S3-MINI-1-N8, CC1101 868 intégré) | `esp32s3` |
| `external` | ESP32-WROOM DevKit + CC1101 externe | `esp32` |
| `m5-atom-lite` | M5Stack ATOM Lite + CC1101 en Dupont | `esp32` |

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

La même chose tourne en intégration continue à chaque poussée, pour les trois
cartes : voir `.github/workflows/firmware.yml`.

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
2. Aucun WiFi configuré → démarre SoftAP `OpenProfalux-Setup` (mdp `openprofalux`)
3. Se connecter au SoftAP + ouvrir http://192.168.4.1
4. Configurer :
   - Device name (=ex `volet_chambre_invitee`)
   - WiFi SSID + password
   - MQTT broker URI (=`mqtt://192.168.1.x:1883`)
5. ESP32 reboot → connect WiFi → connect MQTT → HA discovery auto

## Debug depuis mon côté (=via MQTT)

```bash
# Subscribe à tous les logs et frames émises
mosquitto_sub -h 192.168.1.x -v -t 'openprofalux/#'

# Trigger un pair
mosquitto_pub -h 192.168.1.x -t 'openprofalux/volet_test/pair' -m '{}'

# Voir le state
mosquitto_sub -h 192.168.1.x -t 'openprofalux/volet_test/state'

# Envoyer commande
mosquitto_pub -h 192.168.1.x -t 'openprofalux/volet_test/cmd' -m '{"btn":"UP"}'

# Capture RX pour analyse trames télécommande d'origine
mosquitto_pub -h 192.168.1.x -t 'openprofalux/listen/start' -m ''
mosquitto_sub -h 192.168.1.x -t 'openprofalux/listen/frame' -v
# ... presser bouton sur télécommande d'origine, voir les trames captured ici ...
mosquitto_pub -h 192.168.1.x -t 'openprofalux/listen/stop' -m ''
```

## Logs disponibles

Le firmware publie sur MQTT :
- `openprofalux/log` — logs de toutes activités (=`{"lvl":"info","msg":"..."}`)
- `openprofalux/{device}/state` — state actuel (=serial, counter, rssi, heap)
- `openprofalux/{device}/pair_result` — résultat pairing (=success/timeout)
- `openprofalux/listen/frame` — chaque trame RF captured en mode listen
- `openprofalux/system/status` — heap free, uptime, target

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
