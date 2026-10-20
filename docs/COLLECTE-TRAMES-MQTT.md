# Collecte des trames MQTT

Le boîtier OpenProfalux publie chaque trame reçue sur MQTT en temps réel. Pour rassembler un gros jeu de trames d'une même télécommande, on abonne un enregistreur en direct sur `openprofalux/frames/#` et on le laisse tourner : le flux n'a aucune limite.

## Ce que publie le boîtier

Avec l'écoute RF permanente activée et le MQTT connecté, chaque trame nouvelle est publiée sur deux topics :

- `openprofalux/frames/last` : la dernière trame, pour le capteur Home Assistant.
- `openprofalux/frames/log/<slot>` : la même trame sur un emplacement numéroté, en QoS 1 et retenu.

Pour tout capter, abonne-toi au joker `openprofalux/frames/#`.

Chaque message est un JSON compact :

```json
{"serial":"0x08B22BB","button":"0x4","hop":"0xB1E667D9","rssi":-49,"t":1760961600}
```

- `serial` : numéro de la télécommande.
- `button` : bouton appuyé (montée, descente, stop).
- `hop` : le code tournant chiffré, la donnée utile pour l'analyse.
- `rssi` : niveau de réception, en dBm.
- `t` : horodatage epoch, qui donne l'ordre des appuis.

Prérequis : dans l'interface du boîtier, active « Écoute RF permanente » et vérifie que le MQTT est connecté. Sans ces deux conditions, rien n'est publié.

## Collecter en direct, sans limite

Le flux MQTT est le vrai outil de collecte : chaque nouvelle trame y passe, sans plafond. Abonne un enregistreur en continu et laisse-le tourner.

En ligne de commande :

```sh
mosquitto_sub -h <courtier> -t 'openprofalux/frames/#' -v >> trames.log
```

Sur Home Assistant, une automatisation qui écrit chaque message `openprofalux/frames/#` dans un fichier fait la même chose.

Deux points pratiques :

- Garde une seule télécommande (même numéro de série) pour un jeu cohérent.
- Chaque appui distinct incrémente le compteur et donne une nouvelle trame. Une automatisation qui appuie toute la journée (contact sec, relais) remplit vite le jeu.

Pas besoin de relier la télécommande au moteur : elle fabrique et émet sa trame toute seule à chaque appui, qu'un volet écoute ou non. Le boîtier n'a qu'à recevoir.

## Les limites à connaître

Trois chemins existent, un seul est sans plafond :

| Chemin | Plafond | Pour la collecte |
| --- | --- | --- |
| Flux en direct `openprofalux/frames/#` | aucun | la bonne méthode : laisse l'enregistreur tourner |
| Topics retenus `frames/log/<slot>` | ~1000, ils tournent sur 1000 emplacements | une relecture unique ne rend que les ~1000 derniers |
| Export HTTP `/api/frames` | 1024 trames distinctes par télécommande | dépannage ponctuel, pas pour un gros jeu |

Le boîtier dédoublonne par code tournant (`hop`) : il ignore les répétitions exactes d'un même appui. C'est voulu, deux appuis distincts ont des `hop` distincts, donc ils passent tous.

## À quoi ça sert

Cette collecte alimente l'issue #74 : rassembler un grand nombre de trames d'une même télécommande pour retrouver la clé Profalux par analyse, et rendre OpenProfalux indépendant de toute source extérieure.

Le compteur de synchronisation KeeLoq tient sur 16 bits, soit 65536 valeurs. S'en approcher est l'idéal, mais chaque contribution aide : on cumule entre participants.

Dans chaque trame, le champ `hop` est le code tournant chiffré. Le bouton et l'horodatage donnent l'ordre des appuis, donc la progression du compteur. C'est ce couple qui ouvre l'analyse.
