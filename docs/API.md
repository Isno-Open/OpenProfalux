# OpenProfalux : API MQTT

Toutes les interactions avec le firmware se font via MQTT. Le firmware publie sa présence + entities via Home Assistant MQTT Discovery.

## Topics : commandes entrantes (=subscribe)

### `openprofalux/{device}/pair`
**Trigger** : payload vide OU `{}`.
Démarre un burst de 60 trames sur 60 secondes avec la (serial, crypt_key) du device.

**Workflow user** :
1. Publier `openprofalux/volet_chambre/pair` depuis HA
2. **Immédiatement** : appuyer bouton **PROG** au dos de la télécommande d'origine EMPX-B1 (=~2-3 sec)
3. Moteur fait aller-retour de confirmation → entre en mode learn
4. ESP32 émet trames pendant les ~60s restantes
5. Moteur enregistre ESP32 comme nouvelle télécommande

Publish result sur `openprofalux/volet_chambre/pair_result`.

### `openprofalux/{device}/reset`
**Trigger** : payload `{"confirm": true}`.
Régénère un nouveau (serial, crypt_key) random. **Perd l'appairage actuel**.
À utiliser si l'appairage a échoué et qu'on veut réessayer avec un émetteur "vierge".

### `openprofalux/{device}/cmd`
**Trigger** : `{"btn": "UP"}`, `{"btn": "STOP"}`, `{"btn": "DOWN"}`.
Émet immédiatement 3 trames button avec counter++. Utilisé pour piloter après appairage.

### `openprofalux/listen/start`
Démarre la capture RX. ESP32 met CC1101 en mode RX et publie toute trame KEELOQ 66-bit détectée sur `openprofalux/listen/frame`.

Utile pour :
- Debug : capturer les trames de la télécommande d'origine
- Analyse : voir le format réel du protocole Profalux
- Sync counter : suivre le rolling counter de la télécommande d'origine si le user l'utilise en parallèle

### `openprofalux/listen/stop`
Arrête la capture RX.

## Topics : publications sortantes (=pub)

### `openprofalux/{device}/state` (=retained)
```json
{
  "serial": "0x0A5F2C41",
  "counter": 127,
  "last_cmd": "UP",
  "last_cmd_ts": 1725561234,
  "rssi": -52
}
```

### `openprofalux/{device}/pair_result`
```json
{
  "result": "success" | "timeout",
  "frames_sent": 60,
  "duration_ms": 60000
}
```

### `openprofalux/listen/frame`
```json
{
  "serial": "0x0BAC04D0",
  "button": 4,
  "button_name": "UP",
  "encrypted_hop": "0x4A57F5DF",
  "status_flags": 0,
  "rssi": -58,
  "ts_us": 1234567890
}
```

Si la crypt_key de ce serial est connue (=nôtre ou une captured), on décode aussi :
```json
{
  ...
  "decoded": true,
  "counter": 5,
  "discriminant": 0x1234
}
```

### `openprofalux/system/status` (=retained)
```json
{
  "fw_version": "0.1.0-alpha",
  "uptime_sec": 3600,
  "free_heap": 145632,
  "wifi_rssi": -45,
  "target": "external"
}
```

## Home Assistant MQTT Discovery

Publié automatiquement sur `homeassistant/cover/openprofalux_{device}/config` :

```json
{
  "name": "Volet Chambre Invitée",
  "unique_id": "openprofalux_volet_chambre",
  "device_class": "shutter",
  "command_topic": "openprofalux/volet_chambre/cmd",
  "payload_open":  "{\"btn\":\"UP\"}",
  "payload_close": "{\"btn\":\"DOWN\"}",
  "payload_stop":  "{\"btn\":\"STOP\"}",
  "state_topic":   "openprofalux/volet_chambre/state",
  "value_template": "{{ value_json.last_cmd }}"
}
```

Plus un `button` entity pour trigger le pairing :
```json
{
  "name": "Pair Volet Chambre",
  "unique_id": "openprofalux_volet_chambre_pair",
  "command_topic": "openprofalux/volet_chambre/pair"
}
```

## Exemples curl (=test manuel via mosquitto_pub)

```bash
# Trigger pairing
mosquitto_pub -h ha.local -t 'openprofalux/volet_chambre/pair' -m '{}'

# Piloter
mosquitto_pub -h ha.local -t 'openprofalux/volet_chambre/cmd' -m '{"btn":"UP"}'

# Capture RX pour debug
mosquitto_pub -h ha.local -t 'openprofalux/listen/start' -m ''
# ... presser bouton sur télécommande d'origine ...
mosquitto_sub -h ha.local -t 'openprofalux/listen/frame'
```
