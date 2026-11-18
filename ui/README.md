# Interface web du boîtier (Vue 3)

L'interface que sert le boîtier : rendue dans le navigateur, sur l'API REST de `firmware/main/web_ui.c`.
La construction donne trois fichiers aux noms fixes, `index.html`, `app.js` et `style.css`, dans `ui/dist` :
`firmware/main/CMakeLists.txt` les compresse et les embarque, `web_ui.c` les sert sous ces mêmes noms.

## Construire

Depuis la racine du dépôt :

```bash
tools/build_ui.sh
```

`npm` s'il est installé (Node 22.12 ou plus récent), sinon Docker (`node:22.23.3-alpine`). Les versions des
paquets sont figées par `package-lock.json` (`npm ci`). Rien de construit n'est commis. À refaire après chaque
modification de `ui/` ou de `firmware/main/web/` (`profiles.json`, `pyramid-detect.js`), avant `idf.py`.

## Travailler sur l'interface

```bash
cd ui && npm ci

# contre un vrai boîtier : l'API est relayée vers lui, le code se recharge à chaque enregistrement
BOX=192.168.1.50 npm run dev

# sans carte, sur de fausses données (les mêmes champs JSON que le firmware)
npm run dev:maquette
```

Puis ouvrir l'adresse que donne Vite (http://localhost:5173). Avec les fausses données, `?connexion` dans
l'adresse montre l'écran de connexion (n'importe quel mot de passe sauf « faux »). `npm run build:maquette`
construit cette version dans `ui/dist-maquette`, servable par n'importe quel serveur statique.

## Organisation

- `src/styles/tokens.css` : la charte. Couleurs, rayons et espacements, en sombre et en clair. Les composants
  ne lisent que ces variables.
- `src/components/base/` : les composants génériques (`OpButton`, `OpCard`, `OpField`, `OpSelect`, `OpSwitch`,
  `OpTabs`, `OpSubtabs`, `OpSteps`, `OpDetails`, `OpChip`, `OpTagBox`, `OpTable`, `OpFileInput`, `OpBadge`,
  `OpStatline`, `OpProgress`, `OpToast`, `OpIcon`).
- `src/components/metier/` : les composants d'OpenProfalux, bâtis avec les génériques. Volets : `ShutterTile`,
  `ShutterControl`, `GateControl`, `PositionBar`. Appareils : `AddDevice`, `DetectProposal`, `ProfileChoice`,
  `CaptureSlots`, `TxParamsPanel`, `ChipPicker`, `DeviceRow`, `DeviceEdit`. Système : `RadioList`, `RadioCard`,
  `FreqProfile`, `SweepControl`, `RxGain`, `HealthPanel`, `LiveFramesTable`, `LogView`, `CapturePanel`. Communs :
  `AppHeader`, `LoginScreen`, `BatteryBadge`, `SignalBars`.
- `src/views/` : les écrans, qui assemblent ces composants (`appareils/`, `reseau/`, `systeme/` pour les sections).
- `src/store.js` : la session, le statut (relu toutes les 3 s), la radio occupée, les messages.
- `src/api/` : la seule porte vers le boîtier. `reelle.js` et `reelle-systeme.js` appellent les routes de
  `web_ui.c`, avec les mêmes corps que l'interface d'avant ; `mock.js` et `mock-systeme.js` répondent avec de
  fausses données (développement sans carte, `.env.maquette`).
- `src/profils.js` : tout ce qui se lit dans `firmware/main/web/profiles.json` (types, marques, méthodes, champs
  d'émission, rôles, bornes). Aucun écran ne code en dur un type ou une marque. La table est lue au boîtier après
  la connexion (route protégée) ; les fausses données l'importent depuis le même fichier.
- `src/lib/detection.js` : la détection de profil, `firmware/main/web/pyramid-detect.js` (avec son banc node).
- `src/router.js` : les adresses d'avant restent valides (`#control`, `#remotes/add`, `#sys/log`...). Wi-Fi, MQTT
  et Cloud sont les sections de Réseau ; `#wifi`, `#mqtt` et `#cloud` y mènent (portail captif : `/#wifi`).
- `src/assets/` : les icônes, dessinées pour le projet.

## Règles

- Aucune boîte du navigateur (`alert`, `confirm`, `prompt`) : on confirme dans la page. `tools/check_web_dialogs.py`
  le vérifie en CI.
- Le statut est relu toutes les 3 s : un champ de formulaire copie sa valeur une fois, il ne se lie pas au statut
  (sinon la saisie en cours est écrasée).
- Aucune police téléchargée : le boîtier peut être hors ligne.
- Composants tiers embarqués : Vue et qrcode-generator, sous licence MIT (voir `NOTICE.md`), rappelés en tête de
  `app.js`.
