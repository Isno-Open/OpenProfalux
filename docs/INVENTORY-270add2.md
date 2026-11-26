# Inventaire OpenProfalux à l'état du commit 270add2 (fusion PR #133)

Base de la refonte du modèle des profils (issue #134). Tout est lu dans le code de `270add2`, chaque affirmation porte son `fichier:ligne`. Chemins relatifs à la racine du dépôt. Abréviations : `pj` = `firmware/main/web/profiles.json`, `sh` = `firmware/main/shutters.c`, `wu` = `firmware/main/web_ui.c`.

Rappel du mécanisme général : le firmware embarque `pj` compressé et le sert tel quel sur `/profiles.json` (`firmware/main/CMakeLists.txt:44`, `:110` ; `wu:209-210`, `wu:2029`). **Le C ne lit jamais `pj` à l'exécution** : il n'en garde que des miroirs écrits à la main, vérifiés par des bancs (`firmware/main/cover_attrs.c:8-21`, `firmware/main/cover_attrs.h:120-140`, `sh:871-872`). Les valeurs de `pj` arrivent au firmware **uniquement par l'interface**, qui les lit et les recopie dans les corps de requête ou dans la fiche de l'appareil.

---

## 1. Le fichier `profiles.json`

### 1.1 Forme exacte

| Clé (racine) | Type | Ligne | Contenu |
|---|---|---|---|
| `_contract` | chaîne | `pj:2` | note de conception |
| `devices_max` | nombre | `pj:3` | 24 |
| `name_max` | nombre | `pj:4` | 23 |
| `roles` | objet | `pj:5-11` | `_note`, `central`, `zone` |
| `params` | objet | `pj:12-30` | un objet par trait, plus `_note` |
| `types` | objet | `pj:31-39` | `volet`, `portail` |
| `profils` | tableau | `pj:40-96` | 6 profils |

**`roles`** (`pj:7-10`)

| Rôle | `label` | `members_label` | `members` | `positional_only` | `list_first` | `en.label` / `en.members_label` |
|---|---|---|---|---|---|---|
| `central` | Centrale | Appareils de la centrale | `["simple","zone"]` | absent | `true` | Central / Devices of the central |
| `zone` | Zone | Volets qui suivent la zone | `["simple"]` | `true` | absent | Zone / Shutters following the zone |

**`params`** (`pj:13-28`) : chaque clé porte `statut` (`certain`, `valide`, `intention`, `implementation`) et parfois `detecte`.

| Trait | statut | detecte |
|---|---|---|
| `modulation`, `code`, `mecanisme` | certain | non |
| `band` | certain | `true` |
| `tx.encoder`, `tx.short_us`, `tx.long_us` | intention | non |
| `freq_khz`, `rx.datarate_baud`, `rx.bandwidth_khz`, `tx.repeats`, `tx.gap_us`, `tx.te_us`, `tx.repeat_bit`, `tx.frame_bits` | valide | non |
| `tx.rmt` | implementation | non |

**`types`** (`pj:32-38`)

| Clé | `label` | `code` | `icon` | `positional` | `dpad` (up/stop/down) | `en` | `travel_s` | `capture` |
|---|---|---|---|---|---|---|---|---|
| `volet` | Volet | 0 | emoji | `true` | Montée / Stop / Descente | Shutter, Up/Stop/Down | `{min:1,max:180}` | 3 entrées up, stop, down |
| `portail` | Portail | 1 | SVG à un seul `path` | absent | Ouvrir / Stop / Fermer | Gate, Open/Stop/Close | absent | 1 entrée `{cmd:"up",label:"Bouton du portail"}` |

**`profils`** (`pj:41-95`), clés communes : `type`, `marque`, `nom`, `description`, `mecanisme`, `modulation`, `code`, `band`, `flow`, `freq_khz`, `tx`, `note` ; spécifiques : `rx` (CAME), `endpoint` (Somfy).

| # | type | marque | mecanisme | flow | band | freq_khz | rx | tx | Ligne |
|---|---|---|---|---|---|---|---|---|---|
| 0 | portail | CAME | rejeu | `rejeu` | 433 | 433920 | `{datarate_baud:3800,bandwidth_khz:135}` | `{encoder:"came-top432",short_us:320,long_us:640,repeats:8,gap_us:11500}` | `pj:41-50` |
| 1 | volet | Profalux | cloner | `learn` | 868 | 868425 | non | `{te_us:400,repeat_bit:true,rmt:true}` | `pj:51-59` |
| 2 | volet | Profalux | enrolement | `enrol-pfx` | 868 | 868425 | non | idem | `pj:60-68` |
| 3 | volet | Eveno | enrolement | `enrol-pfx` | 868 | 868330 | non | idem | `pj:69-77` |
| 4 | volet | NeoSol | enrolement | `enrol-pfx` | 868 | 868330 | non | idem | `pj:78-86` |
| 5 | volet | Somfy RTS | enrolement | `create-somfy` (+ `endpoint:"/api/somfy/create"`) | 433 | 433420 | non | `{frame_bits:56}` | `pj:87-95` |

Tous les profils ont `modulation:"OOK"` ; `code` vaut `fixe` pour CAME, `roulant` pour les autres.

### 1.2 Qui lit quoi

Lecteurs : l'interface Vue (`ui/src`), le module de détection `firmware/main/web/pyramid-detect.js` (appelé par l'interface), le générateur du plugin cloud `cloud-plugin/scripts/gen-kinds.ts`, et des bancs (`firmware/test/cover-attrs`, `firmware/test/pyramid-detect`, maquette `ui/src/api/mock.ts:6`).

| Valeur | Lue par | Preuve |
|---|---|---|
| `devices_max` | UI (plafond affiché) ; cloud (borne du rang) ; banc cover-attrs (miroir de `COVER_DEVICES_MAX`) | `ui/src/views/devices/DeviceListSection.vue:30` ; `cloud-plugin/scripts/gen-kinds.ts:45,61` ; `firmware/main/cover_attrs.h:128-130` |
| `name_max` | UI (`maxlength` des champs nom) ; cloud (`NAME_MAX`) | `ui/src/views/devices/CaptureFlow.vue:194`, `EnrolFlow.vue:288`, `SomfyFlow.vue:72`, `CentralSection.vue:75`, `ui/src/components/domain/DeviceEdit.vue:109` ; `gen-kinds.ts:36-40` |
| `roles.central.members`, `.members_label` | UI (écran Centrale) ; cloud | `ui/src/views/devices/CentralSection.vue:21-22,77` ; `gen-kinds.ts:83-96` |
| `roles.*.label`, `.en`, `positional_only`, `list_first` ; **tout `roles.zone`** | cloud seulement. L'UI code en dur la règle de zone (types positionnels, ni centrale ni zone suiveuse) | `gen-kinds.ts:87-94,102` ; `DeviceEdit.vue:46-51` |
| `params[*].statut`, `params[*].detecte` | `pyramid-detect.js` (filtre des traits `certain`) | `firmware/main/web/pyramid-detect.js:35-41,54-65` |
| `types[*].label` | UI (tuiles, filtres, select de type) ; cloud | `ui/src/components/domain/AddDevice.vue:32-35`, `DeviceEdit.vue:30`, `DeviceListSection.vue:25` ; `gen-kinds.ts:48` |
| `types[*].code` | UI (code ↔ clé, envoi à `/api/rejeu/create` et `/api/volet/type`) ; banc cover-attrs | `ui/src/profiles.ts:19-23`, `ui/src/store.ts:186-190`, `CaptureFlow.vue:124`, `DeviceEdit.vue:66` ; `firmware/test/cover-attrs/main/test_main.c:16-24,83-92` |
| `types[*].dpad` | UI (pavé, libellés) ; cloud | `ui/src/components/domain/ShutterTile.vue:67`, `EnrolFlow.vue:85`, `SomfyFlow.vue:41` ; `gen-kinds.ts:48,54-56` |
| `types[*].positional` | UI (calibration, zone possible, tuile) ; cloud (contrôle Position, orientation) | `ui/src/views/devices/CalibrationSection.vue:29`, `DeviceEdit.vue:31`, `ShutterTile.vue:35` ; `gen-kinds.ts:58,63` |
| `types[*].travel_s` | UI (bornes de calibration) ; cloud ; banc cover-attrs (miroir `COVER_TRAVEL_*`) | `CalibrationSection.vue:64-65` ; `gen-kinds.ts:65-72` ; `test_main.c:94-95` |
| `types[*].capture` | UI (cases de capture, libellé du bouton portail) | `CaptureFlow.vue:56-61`, `ShutterTile.vue:44` |
| `types[*].icon` | cloud seulement (l'UI a son propre sprite) | `gen-kinds.ts:27-33,73` ; `ui/src/profiles.ts:16-17` |
| `types[*].en` | cloud seulement | `gen-kinds.ts:49-51` |
| `profils[*].type`, `.marque` | UI (pyramide, profil d'un appareil) | `AddDevice.vue:33-47`, `ui/src/profiles.ts:45-51` |
| `profils[*].nom`, `.description` | UI (cartes de méthode, titres) | `ui/src/components/domain/ProfileChoice.vue:14-15`, `CaptureFlow.vue:157` |
| `profils[*].mecanisme` | UI (profil d'un appareil, appareils compatibles) ; détection (trait certain) | `ui/src/profiles.ts:50`, `CaptureFlow.vue:37`, `DeviceEdit.vue:40` |
| `profils[*].flow` | UI (route du parcours) | `ui/src/profiles.ts:92-98`, `EnrolFlow.vue:70`, `CaptureFlow.vue:51,106` |
| `profils[*].band` | UI (filtre par radios présentes, choix de la radio, profils interchangeables) ; détection (seul trait observé) | `AddDevice.vue:23`, `ui/src/store.ts:166-170`, `DeviceEdit.vue:40`, `ui/src/components/domain/DetectProposal.vue:39` |
| `profils[*].freq_khz` | UI : champ Fréquence pré-rempli, fréquence de capture ; envoyé au firmware | `ui/src/profiles.ts:68`, `CaptureFlow.vue:96-101` |
| `profils[*].rx` | UI → `/api/rejeu/capture` (`datarate_baud`, `bandwidth_khz`) → `cc1101_set_capture_rx` | `CaptureFlow.vue:125`, `ui/src/api/real.ts:64-67`, `wu:1631-1634`, `firmware/main/cc1101.c:960-963` |
| `profils[*].tx.te_us`, `.repeat_bit`, `.rmt`, `.frame_bits` | UI (`txFields`, valeurs par défaut du panneau avancé) | `ui/src/profiles.ts:65-74` |
| `profils[*].modulation`, `.code` | seulement par la détection comme traits `certain`, mais l'observation ne contient que `band` : sans effet aujourd'hui | `DetectProposal.vue:39`, `pyramid-detect.js:61-65` |

**Lu nulle part (ni UI, ni firmware, ni cloud)** : `_contract`, `roles._note`, `params._note`, toutes les `note`, `profils[5].endpoint` (l'URL est en dur dans `ui/src/api/real.ts:111`), `tx.encoder`, `tx.short_us`, `tx.long_us`, `tx.repeats`, `tx.gap_us` (le type `ProfileTx` ne les déclare même pas : `ui/src/api/types.ts:73`).

**Notes de `pj` contredites par le code** :
- `pj:49` dit que `rx` n'est « câblé nulle part » et que `repeats`/`gap_us` sont « câblés ». C'est l'inverse : `rx` part bien au firmware (chaîne ci-dessus), alors que le rejeu émet avec ses propres constantes `REJEU_REPEATS 8` et `REJEU_GAP_US 12000` (`sh:578-579`), jamais lues depuis le profil.
- `params` marque `rx.*` en `valide` mais `pj:29` les cite encore en `intention`.

---

## 2. Par protocole : paramétrable par appareil, et en dur

### 2.1 Fiche paramétrable (commune) : champs de `volet_t`

`firmware/main/cfg_model.h:44-92`. Les champs d'émission par appareil : `freq_khz` (0 = hériter), `te_us` (0 = hériter), `tx_rpt` et `tx_rmt` (tri-état 0 hériter, 1 non, 2 oui, `cfg_model.h:38-42`), `frame_bits` (Somfy, 0 = 56), `marque` (`cfg_model.h:63-68`). Ils s'écrivent par `/api/volet/txparams` (`wu:488-507`) → `shutters_set_tx_params` (`sh:1702-1716`), avec ces bornes : TE hors 200..900 µs ignoré (`sh:1708`), `frame_bits` seulement 56 ou 80 (`sh:1711`), **fréquence acceptée sans contrôle** (`sh:1707`), et toute requête sans `freq_khz` la remet à 0.

Résolution à l'émission : `tx_params_of` (`sh:726-734`) traduit la fiche en `tx_params_t` ; `radio_tx_device` pose fréquence, TE, bit de répétition, RMT sur la radio active, émet, puis restaure (`firmware/main/radio.c:49-67`). Un 0 garde le réglage courant de la radio (réglage global par radio de `/api/config`, `wu:682-686,760-764` ; défauts `TX_TE_DEFAULT_US 400`, rpt et rmt à faux : `firmware/main/hardware_config.h:63`, `firmware/main/cc1101.c:389`).

Le firmware ne code volontairement aucun défaut de profil (`sh:720-725`) : c'est le navigateur qui grave les valeurs du profil dans la fiche.

### 2.2 KeeLoq clone (profil « Cloner ma télécommande Profalux », mécanisme `cloner`)

| Aspect | Où | Valeur |
|---|---|---|
| Capture | `/api/learn/start?radio=&freq_khz=` puis `/api/learn/poll` puis `/api/learn/assign` | `wu:396-428` ; `ui/src/api/real.ts:71-80` |
| Fréquence de capture | paramètre `freq_khz` (celle du profil ou du panneau) | `wu:400-404` ; `firmware/main/radio.c:126-136` |
| Stockage | 3 chaînes de bits `up`/`stop`/`down` + bouton appris + serials | `sh:1332-1354`, `cfg_model.h:46-49` |
| Émission | rejoue la trame captée, `PRESS_REPEATS 10` trames par appui | `sh:542`, `sh:552-571`, `sh:819,835` |
| Codage sur l'air (en dur) | préambule 23 alternances de TE, en-tête 10 TE, bit 1 = TE haut / 2 TE bas, bit 0 = 2 TE haut / TE bas, gap 2 ms, ou 18 ms porteuse coupée si bit de répétition | `firmware/main/hardware_config.h:60-64` ; `firmware/main/cc1101.c:118,658-688` |
| Paramétrable par appareil | `freq_khz`, `te_us`, `tx_rpt`, `tx_rmt` (gravés à la capture depuis la pyramide) | `CaptureFlow.vue:132-137` |
| Création alternative | « Créer un volet » depuis une télécommande du journal RF (Captées), trames reconstruites, bouton haut/stop/bas en dur | `sh:1647-1677`, `wu:561`, `ui/src/views/devices/CapturedSection.vue:36` |
| Garde-fou | un appareil cloné refuse la trame d'une autre télécommande (côté UI) | `ui/src/api/real.ts:75` |

Codes boutons utilisés pour reconnaître haut/stop/bas : `PFX_BTN_UP 0x2`, `PFX_BTN_STOP 0x4`, `PFX_BTN_DOWN 0x8` (`firmware/main/pfx_enrol.h:29-31`).

### 2.3 KeeLoq enrôlement virtuel (Profalux, Eveno, NeoSol ; mécanisme `enrolement`, flow `enrol-pfx`)

| Aspect | Où | Valeur |
|---|---|---|
| Capture de la télécommande d'origine | `/api/pfx/capture` (corps : `model`, `freq_khz`, `te_us`, `tx_repeat_bit`, `tx_rmt`, `marque`) → écoute 15 s | `wu:1368-1400`, `wu:1334-1355` ; `EnrolFlow.vue:122-130` |
| Attribution d'identité | table persistante télécommande → emplacement + compteur, 63 emplacements, compteur neuf à 2 | `firmware/main/pfx_enrol.c:47-59,180-216` |
| Resynchronisation d'un compteur non confirmé | sauts croissants 1024, 4096, 16384 | `firmware/main/pfx_enrol.c:75,189-198` |
| Identité neuve à la place | `/api/pfx/renew` | `wu:1424-1433`, `pfx_enrol.c:223-239` |
| Trame d'apprentissage | bouton 0x5, `PFX_BURST 9` trames × `PFX_LEARN_ROUNDS 5` salves, 15 ms entre salves (~5 s) | `pfx_enrol.c:26-32,377-399` |
| Porte d'apprentissage | émission refusée sans capture réussie dans la tentative | `wu:1328-1332,1450-1465` |
| Test pendant l'enrôlement | `/api/pfx/cmd` up/stop/down, rafale de 9 | `pfx_enrol.c:401-414` |
| Enregistrement | `/api/pfx/save_volet` → `shutters_create_virtual` puis grave `freq`, `te`, `rpt`, `rmt`, `marque` du profil | `wu:1645-1666`, `sh:1388-1411` |
| Commandes ensuite | trame générée à chaque appui, compteur réservé puis émis, `PRESS_REPEATS 10` | `sh:736-756` |
| Construction de trame (en dur) | 66 bits, statut fixé à « répétition = 1 » | `pfx_enrol.c:335-351` |
| Paramétrable par appareil | `freq_khz`, `te_us`, `tx_rpt`, `tx_rmt`, `marque` | `wu:1659-1660` |

**Modèles et routines (en dur dans `pfx_enrol.c:37-42`)** : 4 modèles, exposés par `/api/pfx` (`wu:1434-1449`) : trois références Profalux (routine R6, R8 pour NeoSol, R7) et un modèle de moteur tiers (R6). Le modèle ne change **que** la chorégraphie (`pfx_enrol.c:34-36`), pas l'émission.

Les **gestes** de chaque routine sont **en dur dans l'UI**, pas dans le firmware ni dans `pj` : `GESTURES` R6/R7/R8, geste préalable `BEFORE.R8`, délai moteur 10 s, fenêtre de capture 15 s, émission 5 s (`EnrolFlow.vue:47-60`). Le modèle par défaut est choisi en cherchant le nom de la marque dans le nom du modèle (`EnrolFlow.vue:114`).

### 2.4 Somfy RTS (flow `create-somfy`)

| Aspect | Où | Valeur |
|---|---|---|
| Création | `/api/somfy/create {id, address}`, adresse 24 bits saisie ou tirée au hasard par l'UI | `wu:1550-1599` ; `SomfyFlow.vue:44-58` |
| Refus | adresse qui ressemble à une identité virtuelle Profalux ; nom déjà pris ; virgule | `sh:1416-1449` ; `wu:1576-1588` |
| Code roulant | part à 1, réservé puis émis | `sh:1442`, `sh:763-773` |
| Appairage | commande `prog` sur `/api/shutter` | `sh:1131-1141` ; `SomfyFlow.vue:60-63` |
| Boutons (en dur) | haut → UP 0x2, bas → DOWN 0x4, stop → MY 0x1, PROG 0x8 | `firmware/main/somfy_tx.c:27-33`, `firmware/main/somfy_rts.h:40-49` |
| Timing (en dur) | fréquence 433420, réveil 10920/7357 µs, sync matériel 2560, sync logiciel 4850, symbole 1280, silence inter-trame 27434, 2 paires de sync puis 7 | `somfy_rts.h:36,59-67` |
| Répétitions (en dur) | `SOMFY_RTS_TX_REPEATS 1` (2 trames), plafond 7 | `firmware/main/somfy_tx.h:19,25` |
| Paramétrable par appareil | `frame_bits` 56 ou 80, `freq_khz` (0 = 433420) | `sh:778-779` ; `somfy_tx.c:54,80` |

**Écart** : `SomfyFlow` n'utilise ni l'index du profil de l'adresse ni ses valeurs ; il n'envoie que `{id, address}` (`SomfyFlow.vue:52`). La fiche d'un volet Somfy naît donc **sans marque, fréquence 0, `frame_bits` 0** ; l'UI retrouve la marque par le protocole (`ui/src/profiles.ts:45`). Dans l'édition, la fréquence Somfy est « fixe » (`ui/src/profiles.ts:68`, marque en dur `BRAND.somfy`) et n'est donc jamais envoyée.

### 2.5 Rejeu brut CAME (flow `rejeu`)

| Aspect | Où | Valeur |
|---|---|---|
| Création | `/api/rejeu/create {id, type}` (type = `types.portail.code`) | `wu:1600-1619`, `sh:1453-1466` ; `ui/src/api/real.ts:61` |
| Capture | `/api/rejeu/capture {id, cmd, radio, freq_khz, datarate_baud, bandwidth_khz}`, 15 s, 512 impulsions au plus | `wu:1620-1644`, `sh:1487-1505`, `sh:577` |
| Stockage | JSON `{f, up, stop, down}`, impulsions signées en µs | `cfg_model.h:76-79`, `sh:623-641` |
| Émission | `REJEU_REPEATS 8`, silence `REJEU_GAP_US 12000` ajouté, fréquence = fiche sinon `f` captée | `sh:577-620` |
| Bouton unique | toute commande non capturée rejoue la première présente (up, stop, down) | `sh:590-596` |
| Paramétrable par appareil | `freq_khz` seulement (et `marque`) | `sh:588` ; `CaptureFlow.vue:132-137` |

L'encodeur CAME propre (`firmware/main/came_tx.c/.h`, 320/640 µs, 12 bits, 433920, 8 répétitions : `came_tx.h:22-27`) est **compilé mais appelé nulle part** (`firmware/main/CMakeLists.txt:81` ; aucun `#include "came_tx.h"` hors de `came_tx.c`).

---

## 3. Les procédures d'ajout

### 3.1 Écrans et adresses

`ui/src/views/DevicesView.vue:17-24`, sections dans `ui/src/nav.ts:26-37`, adresses dans `ui/src/profiles.ts:92-98`.

| Adresse | Écran | Déclenché par |
|---|---|---|
| `#remotes/add[/<type>[/<marque>]]` | `AddDevice.vue` : pyramide Type → Marque → Méthode | `AddDevice.vue:54-97` |
| `#remotes/learn/<index>` | `CaptureFlow.vue` (flows `learn` et `rejeu`) | `profiles.ts:94` |
| `#remotes/learn/-/<appareil>` | `CaptureFlow.vue` sans profil (appareil existant) | `DeviceEdit.vue:93` |
| `#remotes/enrol/<index>` | `EnrolFlow.vue` | `profiles.ts:95` |
| `#remotes/somfy/<index>` | `SomfyFlow.vue` (index ignoré) | `profiles.ts:96` |
| `#remotes/central` | `CentralSection.vue` | `nav.ts:29` |
| `#remotes/list` | `CapturedSection.vue` (créer depuis une télécommande captée) | `CapturedSection.vue:36` |

Pyramide : un type sans méthode disponible est grisé avec la bande qui manque (`AddDevice.vue:30-37`) ; seuls les profils dont la bande existe sur la carte sont proposés (`AddDevice.vue:21-23`) ; une marque à méthode unique mène droit au parcours (`AddDevice.vue:46-50`). Détection : écoute sur chaque radio via `/api/capture/start` puis `/api/capture/last`, classement par la seule bande entendue (`DetectProposal.vue:29,39` ; `ui/src/api/real.ts:55`).

### 3.2 Étapes et routes par parcours

| Parcours | Étapes UI | Routes |
|---|---|---|
| Clone KeeLoq | choisir appareil ou « + Nouveau », nom, nom de télécommande, réglages avancés et « Caler sur ma télécommande », une capture par commande du type | `/api/learn/start`, `/api/learn/poll`, `/api/remote`, `/api/learn/assign`, `/api/learn/reassign`, `/api/volet/txparams`, `/api/rx/sweep` |
| Rejeu CAME | idem, une seule capture (type portail) | `/api/rejeu/create`, `/api/rejeu/capture`, `/api/volet/txparams` |
| Enrôlement | 5 étapes : Capturer, Émettre, Gestes, Tester, Enregistrer (`EnrolFlow.vue:231`) | `/api/pfx`, `/api/pfx/capture`, `/api/pfx/capture/poll`, `/api/pfx/renew`, `/api/pfx/learn`, `/api/pfx/cmd`, `/api/pfx/save_volet`, `/api/pfx/forget` |
| Somfy | 3 étapes : Créer, Appairer, Tester (`SomfyFlow.vue:70`) | `/api/somfy/create`, `/api/shutter` (`prog`, puis up/stop/down) |
| Centrale | nom + membres | `/api/central` |
| Zone | dans l'édition d'un appareil | `/api/volet/followers` |

Toutes les routes : `wu:2026-2103`. Seule `/api/learn/adopt` n'est appelée par aucun écran Vue.

### 3.3 Textes en dur (hors `pj`)

- Repli des phrases de méthode `METHOD_SENTENCE` par flow (`ui/src/profiles.ts:102-107`).
- Libellés des champs d'émission et options 56/80 bits (`ui/src/profiles.ts:68-72`).
- `SHUTTER_ACTIONS` de repli, titres et sous-titres, consignes de capture (`CaptureFlow.vue:25,157-168`), message « Le clonage ne pilote pas ton moteur ? » (`CaptureFlow.vue:224-226`).
- Tout le texte d'enrôlement, gestes R6/R7/R8, consigne NeoSol avant émission, messages par code retour (`EnrolFlow.vue:47-63,133-145,225-293`).
- Écran Somfy : « 433,42 MHz », consigne PROG, format d'adresse (`SomfyFlow.vue:35-37,67,88-89`).
- Retour vers `#remotes/add/volet` en dur pour enrôlement et Somfy (`EnrolFlow.vue:226`, `SomfyFlow.vue:68`).
- Constantes de domaine (`ui/src/domain.ts:22-49`), voir section 5.

---

## 4. La fiche d'un appareil (export/import JSON)

Export : `cfg_model_export` (`firmware/main/cfg_model.c:105-112`), document `{log_frames, remotes:{serial:nom}, volets:[...]}` (`cfg_model.c:98-104`). Un document par appareil en NVS (`cfg_model.c:121-134`).

| Clé | Écrite | Condition d'écriture | Lue (import) |
|---|---|---|---|
| `id` | `cfg_model.c:53` | toujours | `:157` |
| `serials` | `:54-55` | toujours | `:166-168` |
| `cmd.up/down/stop` | `:56-59` | toujours | `:169-174` |
| `up_btn`, `down_btn`, `stop_btn` | `:60-62` | toujours | `:175-177` |
| `travel_up_ms`, `travel_down_ms` | `:63-64` | toujours | `:178-179` |
| `orientation` | `:65` | toujours | `:180-181` (absent = -1) |
| `order` | `:66` | toujours | `:183-184` (absent = -1) |
| `position` | `:67` | toujours | `:185` |
| `virt`, `virt_serial`, `virt_counter` | `:68-71` | si virtuel | `:186-193` |
| `proto` | `:74-75` | si virtuel et ≠ 0 (0 Profalux, 1 Somfy : `cfg_model.h:27-30`) | `:191-192` |
| `type` | `:79-80` | si ≠ 0 | `:196-197` (absent = volet) |
| `central` | `:81` | si centrale | `:198` |
| `members` | `:84` | centrale (même vide) ou zone | `:202-205` |
| `rejeu` | `:85` | si rejeu | `:206-207` |
| `freq_khz`, `te_us`, `tx_rpt`, `tx_rmt`, `frame_bits` | `:89-93` | si ≠ 0 | `:210-214` |
| `marque` | `:94` | si non vide | `:215` |

`/api/status` publie une vue proche (`sh:2071-2131`) : `type` toujours, `proto` en texte `"somfy"`/`"profalux"`, `zone`, `follows`, `rejeu_cmds`.

**Le champ `type`** : `uint8_t` dans `volet_t` (`cfg_model.h:56`).
- Fixé à la création : 0 par `get_or_create` (`sh:284`) pour clone, enrôlement, Somfy, centrale, création depuis Captées ; valeur reçue pour le rejeu (`sh:1459`, **sans borne** : `wu:1605,1609`).
- Modifié par `/api/volet/type` (`wu:384-393`) → `shutters_set_type`, borné à `TYPE_PORTAIL` (`sh:1470-1482`), proposé dans l'édition pour un appareil ni centrale ni zone quand plus d'un type existe (`DeviceEdit.vue:65-69,116`).
- L'enrôlement et Somfy ne transmettent aucun type : un appareil enrôlé est toujours un volet à la création.

---

## 5. Les types (volet, portail)

| Lieu | Déclaration | Preuve |
|---|---|---|
| `pj` | `types.volet.code 0`, `types.portail.code 1` | `pj:32,36` |
| C | `TYPE_VOLET = 0`, `TYPE_PORTAIL = 1` | `firmware/main/dev_type.h:10-13` |
| C | noms miroirs `"volet"`, `"portail"` | `firmware/main/cover_attrs.c:11-14` |
| TS | `DEVICE_TYPE = {shutter:'volet', gate:'portail'}`, `DEVICE_TYPE_CODE = {shutter:0, gate:1}` | `ui/src/domain.ts:22-24` |
| TS | type de repli « Volet », code 0, dpad en dur | `ui/src/store.ts:185` |
| TS | icônes par type | `ui/src/profiles.ts:16` |
| Banc | cohérence enum ↔ `pj` | `firmware/test/cover-attrs/main/test_main.c:83-92` |

**Ce qu'un type change** :
- UI : libellés du pavé (`ShutterTile.vue:67`), bouton unique pour le portail (`ShutterTile.vue:31,66` ; `GateControl.vue:11` envoie toujours `up`), cases de capture (`CaptureFlow.vue:56-61`), appareils compatibles en capture (`CaptureFlow.vue:37`), calibration et zone réservées aux types `positional` (`CalibrationSection.vue:29`, `DeviceEdit.vue:31,48`), filtres de liste (`DeviceListSection.vue:22-31`), icône.
- Cloud : `attrs.type` choisit le type de carte du portail (`cover_attrs.c:59`, `cloud-plugin/src/index.ts:119-121`).
- Home Assistant : **rien**. La découverte publie `device_class:"shutter"` pour tout appareil (`sh:1029`) ; le commentaire de `shutters_set_type` qui évoque un libellé HA dépendant du type (`sh:1477`) ne correspond à aucun code.
- Émission : **rien** (`firmware/main/dev_type.h:5-9`). Et `positional` côté firmware ne regarde pas le type : `volet_positional` = écoute permanente + ni centrale ni zone + étalonné (`sh:877-880`). Un portail étalonné serait donc publié positionnel.

---

## 6. Home Assistant et cloud

### 6.1 Découverte MQTT (`announce_one`, `sh:990-1041`)

- Sujet `homeassistant/cover/openprofalux_<slug>/config`, entité `cover`, `device_class:"shutter"`, `optimistic:true`.
- `command_topic openprofalux/cover/<slug>/set`, charges `OPEN`/`CLOSE`/`STOP`.
- Position (`position_topic`, `set_position_topic`, 100 = ouvert) **seulement si** `volet_positional` (`sh:1015-1019`). Centrales et zones jamais.
- `state_topic` : `opening`/`closing`/`open`/`closed`/`stopped` (`sh:859-865`).
- Disponibilité `openprofalux/<boitier>/status`, appareil HA unique « OpenProfalux » (`sh:1034-1036`).
- Entités annexes : interrupteur « Ecoute RF permanente », capteur « Derniere trame RF » (`sh:1046-1067`), et un `binary_sensor` pile par télécommande nommée (`sh:956-988`).
- Réception : `set` OPEN/CLOSE/STOP → `up`/`down`/`stop` ; `set_position` (`sh:2688-2692`). Un portail reçoit donc OPEN/CLOSE qui rejouent la même trame (`sh:590-596`).
- Effacement à la suppression (`sh:1074-1085`).

### 6.2 Cloud

- `openprofalux/cover/<slug>/attrs` : `{type, positional, name, travel_up_s, travel_down_s, rank, orientation, role, members}` (`sh:886-913`, `cover_attrs.c:54-100`).
- Relais avec préfixe par boîtier ; trames, compteurs et verrous restent locaux (`firmware/main/cloud_topic.h:8-14`).
- Le plugin engendre ses types depuis `pj` (`types`, `roles`, `devices_max`, `name_max`) ; une centrale ou une zone devient le type `<type>-<role>` (`cloud-plugin/src/ids.ts:8-12`, `gen-kinds.ts:98-113`).
- Commandes du portail : `cover/<id>/set`, `set_position`, `config {travel_up_ms, travel_down_ms, orientation, rank, members}` (`cloud-plugin/src/index.ts:19-22,174-184` ; `sh:2626-2675`). Le portail ne crée ni ne supprime une centrale ou une zone, il en change les membres (`pj:6`).

---

## 7. Protection de l'émission (bande, fréquence)

**Ce qui existe** :
- Chaque carte déclare `bands_allowed_khz` (par exemple 433050..434790 et 863000..870000 sur la Super) ; `tools/gen_board.py:120-128` les grave dans la table des radios (`band_low_khz`, `band_high_khz`).
- Le **profil radio global** d'une radio (`/api/config`, `radio_profile` et fréquence « custom ») est vérifié contre cette plage : `radio_profile_resolve` refuse hors bande (`firmware/main/radio_profile.h:37-41`, `wu:672-675,707` ; démarrage `firmware/main/main.c:299,347`).
- Le balayage « Caler sur ma télécommande » reste dans la bande déclarée de la radio (`firmware/main/radio.c:333-342`).
- `radio_tx_ook_pulses` choisit la radio dont la bande contient la fréquence (`firmware/main/radio.c:180`, `cc1101.c:61-65`).
- Garde-fou de collision d'identités : une adresse Somfy qui ressemble à une identité Profalux est refusée (`sh:1424`, `wu:1580`).

**Ce qui manque (émission possible hors bande)** :
- `freq_khz` **par appareil** n'est jamais confronté à la bande : ni à l'écriture (`sh:1707`, `wu:1375` pour l'enrôlement), ni à l'émission (`radio_tx_device` pose la fréquence sans contrôle, `firmware/main/radio.c:57` ; `cc1101_set_freq_khz` n'a aucune borne, `cc1101.c:463-480`).
- `radio_tx_ook_pulses` : si aucune radio ne couvre la fréquence, il **retune la radio active** au lieu de refuser (`firmware/main/radio.c:181`). Un rejeu ou un Somfy à une fréquence quelconque part donc sur la radio 868.
- Le champ Fréquence de l'UI n'a ni `min` ni `max` (`TxParamsPanel.vue:100`).
- `max_dbm` de la carte n'est lu par aucun code : la puissance est fixée à `0xC0` dans le PATABLE (`cc1101.c:315-316,339-341`).

---

## 8. Rôles centrale et zone

| | Centrale | Zone |
|---|---|---|
| Ce que c'est | appareil sans émission propre, diffuse up/stop/down à ses membres | appareil appris dont la propre trame fait bouger ses suiveurs, qui suivent sans émettre |
| Représentation | `central=true` + `members` | `members` non vide sur un appareil non central (`sh:262`) |
| Création | `/api/central` (`wu:1503-1522`, `sh:1573-1596`), écran Centrale | `/api/volet/followers` (`wu:1524-1542`, `sh:1511-1542`), édition d'un appareil |
| Membres permis (firmware) | ni elle-même ni une centrale (`sh:1555-1557`) | ni elle-même, ni centrale, ni zone ; une zone ne suit rien (`sh:1522-1528`) |
| Membres permis (UI) | rôles de `roles.central.members` (`CentralSection.vue:19-22`) | types `positional` seulement, en dur (`DeviceEdit.vue:46-51`) |
| Commande | boucle `start_move`/`do_stop` sur chaque membre (`sh:1115-1130`) | `zone_follow` après sa propre émission (`sh:829,846,930-941`) |
| Position | aucune (HA et cloud) | aucune ; `pos` refusé (`sh:1142-1145`) |
| Publication | `attrs.role`, `attrs.members` en slugs (`sh:895-909`) | idem |

Miroirs : `cover_role_t` et `COVER_ROLE_NAMES` (`firmware/main/cover_attrs.h:140`, `cover_attrs.c:17-21`) ; `ROLE` côté UI (`ui/src/domain.ts:42`).

---

## Points à ne pas perdre dans la refonte (synthèse)

1. Le firmware ne lit pas `pj` : toute valeur de profil qui compte doit être gravée dans la fiche ou passée en requête. Le modèle cible doit dire qui grave quoi.
2. Valeurs de protocole en dur et non déclarées : répétitions et silence du rejeu (`sh:578-579`), rafales KeeLoq (`sh:542`, `pfx_enrol.c:27,30`), répétitions Somfy (`somfy_tx.h:25`), codes boutons, modèles et routines (`pfx_enrol.c:37-42`), gestes (`EnrolFlow.vue:47-57`).
3. Somfy n'utilise pas son profil à la création (`SomfyFlow.vue:52`).
4. Le type n'atteint pas Home Assistant, et `positional` firmware ignore le type.
5. Aucune borne de bande sur la fréquence par appareil, ni à l'émission.
6. Les notes de `pj` sur `rx` et `repeats`/`gap_us` sont fausses à cet état.
